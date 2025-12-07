#include "core/common.hpp"
#include "core/status.hpp"
#include "indexing/btree.hpp"
#include "indexing/node.hpp"
#include "storage/wal.hpp"
#include <cstddef>
#include <fmt/core.h>
#include <memory>
#include <optional>
#include <vector>

namespace embrace::indexing {

    Btree::Btree(const std::string &wal_path) : wal_path_(wal_path), recovering_(false) {
        root_ = std::make_unique<LeafNode>();

        if (!wal_path_.empty()) {
            wal_writer_ = std::make_unique<storage::WalWriter>(wal_path_);

            if (!wal_writer_->is_open()) {
                fmt::print(stderr, "WARNING: WAL writer failed to open, continuing without WAL\n");
                wal_writer_.reset();
            }
        }
    }

    Btree::~Btree() {
        if (wal_writer_) {
            auto flush_status = wal_writer_->flush();
            if (!flush_status.ok()) {
                fmt::print(stderr, "ERROR: WAL flush failed in destructor: {}\n",
                           flush_status.to_string());
            }

            auto sync_status = wal_writer_->sync();
            if (!sync_status.ok()) {
                fmt::print(stderr, "ERROR: WAL sync failed in destructor: {}\n",
                           sync_status.to_string());
            }
        }
    }

    auto Btree::get(const core::Key &key) -> std::optional<core::Value> {
        LeafNode *leaf = find_leaf(key);
        if (!leaf)
            return std::nullopt;

        int idx = leaf->get_index(key);
        if (idx != -1) {
            return leaf->values[static_cast<size_t>(idx)];
        }
        return std::nullopt;
    }

    auto Btree::find_leaf(const core::Key &key) -> LeafNode * {
        Node *current = root_.get();

        while (!current->is_leaf()) {
            auto *internal = static_cast<InternalNode *>(current);

            auto it = std::upper_bound(internal->keys.begin(), internal->keys.end(), key);
            auto idx = std::distance(internal->keys.begin(), it);

            current = internal->children[static_cast<size_t>(idx)].get();
        }
        return static_cast<LeafNode *>(current);
    }

    auto Btree::put(const core::Key &key, const core::Value &value) -> core::Status {
        if (wal_writer_ && !recovering_) {
            auto wal_status = wal_writer_->write_put(key, value);
            if (!wal_status.ok()) {
                return wal_status;
            }
        }

        LeafNode *leaf = find_leaf(key);

        int idx = leaf->get_index(key);
        if (idx != -1) {
            leaf->values[static_cast<size_t>(idx)] = value;
            return core::Status::Ok();
        }

        leaf->insert(key, value);

        if (leaf->keys.size() >= max_degree_) {
            split_leaf(leaf);
        }

        return core::Status::Ok();
    }

    auto Btree::update(const core::Key &key, const core::Value &value) -> core::Status {
        LeafNode *leaf = find_leaf(key);
        int idx = leaf->get_index(key);

        if (idx == -1) {
            return core::Status::NotFound(fmt::format("Key: '{}' not found for update", key));
        }
        if (wal_writer_ && !recovering_) {
            auto wal_status = wal_writer_->write_update(key, value);
            if (!wal_status.ok()) {
                return wal_status;
            }
        }

        leaf->values[static_cast<size_t>(idx)] = value;
        return core::Status::Ok();
    }

    auto Btree::remove(const core::Key &key) -> core::Status {
        LeafNode *leaf = find_leaf(key);
        int idx = leaf->get_index(key);

        if (idx == -1) {
            return core::Status::NotFound(fmt::format("Key: '{}' not found for deletion", key));
        }
        if (wal_writer_ && !recovering_) {
            auto wal_status = wal_writer_->write_delete(key);
            if (!wal_status.ok()) {
                return wal_status;
            }
        }

        leaf->keys.erase(leaf->keys.begin() + idx);
        leaf->values.erase(leaf->values.begin() + idx);

        if (leaf != root_.get() && leaf->keys.size() < get_min_keys()) {
            rebalance_after_delete(leaf);
        }

        if (root_->is_leaf() && static_cast<LeafNode *>(root_.get())->keys.empty()) {
            return core::Status::Ok();
        }

        if (!root_->is_leaf()) {
            auto *internal_root = static_cast<InternalNode *>(root_.get());
            if (internal_root->keys.empty() && internal_root->children.size() == 1) {
                auto new_root = std::move(internal_root->children[0]);
                new_root->parent = nullptr;
                root_ = std::move(new_root);
            }
        }
        return core::Status::Ok();
    }

    auto Btree::rebalance_after_delete(Node *node) -> void {
        if (node == root_.get()) {
            return;
        }

        if (!node->is_leaf()) {
            handle_underflow_internal(static_cast<InternalNode *>(node));
            return;
        }

        auto *leaf = static_cast<LeafNode *>(node);
        auto *parent = static_cast<InternalNode *>(leaf->parent);

        size_t leaf_idx = 0;
        for (size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() == leaf) {
                leaf_idx = i;
                break;
            }
        }

        // Try borrowing from right sibling first
        if (leaf_idx + 1 < parent->children.size()) {
            auto *right_sibling = static_cast<LeafNode *>(parent->children[leaf_idx + 1].get());
            if (right_sibling->keys.size() > get_min_keys()) {
                borrow_from_right(leaf, right_sibling, parent, leaf_idx);
                return;
            }
        }

        // Try borrowing from left sibling
        if (leaf_idx > 0) {
            auto *left_sibling = static_cast<LeafNode *>(parent->children[leaf_idx - 1].get());
            if (left_sibling->keys.size() > get_min_keys()) {
                borrow_from_left(leaf, left_sibling, parent, leaf_idx - 1);
                return;
            }
        }

        // Can't borrow, must merge
        if (leaf_idx > 0) {
            auto *left_sibling = static_cast<LeafNode *>(parent->children[leaf_idx - 1].get());
            merge_with_left(leaf, left_sibling, parent, leaf_idx - 1);
        } else {
            auto *right_sibling = static_cast<LeafNode *>(parent->children[leaf_idx + 1].get());
            merge_with_right(leaf, right_sibling, parent, leaf_idx);
        }
    }

    auto Btree::borrow_from_left(LeafNode *node, LeafNode *left_sibling, InternalNode *parent,
                                 size_t parent_key_idx) -> void {
        node->keys.insert(node->keys.begin(), left_sibling->keys.back());
        node->values.insert(node->values.begin(), left_sibling->values.back());

        left_sibling->keys.pop_back();
        left_sibling->values.pop_back();

        parent->keys[parent_key_idx] = node->keys.front();
    }

    auto Btree::borrow_from_right(LeafNode *node, LeafNode *right_sibling, InternalNode *parent,
                                  size_t parent_key_idx) -> void {
        node->keys.push_back(right_sibling->keys.front());
        node->values.push_back(right_sibling->values.front());

        right_sibling->keys.erase(right_sibling->keys.begin());
        right_sibling->values.erase(right_sibling->values.begin());

        parent->keys[parent_key_idx] = right_sibling->keys.front();
    }

    auto Btree::merge_with_left(LeafNode *node, LeafNode *left_sibling, InternalNode *parent,
                                size_t parent_key_idx) -> void {
        left_sibling->keys.insert(left_sibling->keys.end(), node->keys.begin(), node->keys.end());
        left_sibling->values.insert(left_sibling->values.end(), node->values.begin(),
                                    node->values.end());

        left_sibling->next = node->next;
        if (node->next) {
            node->next->prev = left_sibling;
        }

        parent->keys.erase(parent->keys.begin() + static_cast<std::ptrdiff_t>(parent_key_idx));
        parent->children.erase(parent->children.begin() +
                               static_cast<std::ptrdiff_t>(parent_key_idx) + 1);

        if (parent != root_.get() && parent->keys.size() < get_min_keys()) {
            rebalance_after_delete(parent);
        }
    }

    auto Btree::merge_with_right(LeafNode *node, LeafNode *right_sibling, InternalNode *parent,
                                 size_t parent_key_idx) -> void {
        node->keys.insert(node->keys.end(), right_sibling->keys.begin(), right_sibling->keys.end());
        node->values.insert(node->values.end(), right_sibling->values.begin(),
                            right_sibling->values.end());

        node->next = right_sibling->next;
        if (right_sibling->next) {
            right_sibling->next->prev = node;
        }

        parent->keys.erase(parent->keys.begin() + static_cast<std::ptrdiff_t>(parent_key_idx));
        parent->children.erase(parent->children.begin() +
                               static_cast<std::ptrdiff_t>(parent_key_idx) + 1);

        if (parent != root_.get() && parent->keys.size() < get_min_keys()) {
            rebalance_after_delete(parent);
        }
    }

    auto Btree::handle_underflow_internal(InternalNode *node) -> void {
        if (node == root_.get()) {
            return;
        }

        auto *parent = static_cast<InternalNode *>(node->parent);

        size_t node_idx = 0;
        for (size_t i = 0; i < parent->children.size(); i++) {
            if (parent->children[i].get() == node) {
                node_idx = i;
                break;
            }
        }

        // Try borrowing from right sibling
        if (node_idx + 1 < parent->children.size()) {
            auto *right_sib = static_cast<InternalNode *>(parent->children[node_idx + 1].get());
            if (right_sib->keys.size() > get_min_keys()) {
                node->keys.push_back(parent->keys[node_idx]);
                parent->keys[node_idx] = right_sib->keys.front();

                node->children.push_back(std::move(right_sib->children.front()));
                node->children.back()->parent = node;

                right_sib->keys.erase(right_sib->keys.begin());
                right_sib->children.erase(right_sib->children.begin());
                return;
            }
        }

        // Try borrowing from left sibling
        if (node_idx > 0) {
            auto *left_sib = static_cast<InternalNode *>(parent->children[node_idx - 1].get());
            if (left_sib->keys.size() > get_min_keys()) {
                node->keys.insert(node->keys.begin(), parent->keys[node_idx - 1]);
                parent->keys[node_idx - 1] = left_sib->keys.back();

                node->children.insert(node->children.begin(), std::move(left_sib->children.back()));
                node->children.front()->parent = node;

                left_sib->keys.pop_back();
                left_sib->children.pop_back();
                return;
            }
        }

        // Must merge
        if (node_idx > 0) {
            auto *left_sib = static_cast<InternalNode *>(parent->children[node_idx - 1].get());

            left_sib->keys.push_back(parent->keys[node_idx - 1]);
            left_sib->keys.insert(left_sib->keys.end(), node->keys.begin(), node->keys.end());

            for (auto &child : node->children) {
                child->parent = left_sib;
                left_sib->children.push_back(std::move(child));
            }

            parent->keys.erase(parent->keys.begin() + static_cast<std::ptrdiff_t>(node_idx) - 1);
            parent->children.erase(parent->children.begin() +
                                   static_cast<std::ptrdiff_t>(node_idx));

            if (parent != root_.get() && parent->keys.size() < get_min_keys()) {
                rebalance_after_delete(parent);
            }
        } else {
            // Merge with right sibling
            auto *right_sib = static_cast<InternalNode *>(parent->children[node_idx + 1].get());

            node->keys.push_back(parent->keys[node_idx]);
            node->keys.insert(node->keys.end(), right_sib->keys.begin(), right_sib->keys.end());

            for (auto &child : right_sib->children) {
                child->parent = node;
                node->children.push_back(std::move(child));
            }

            parent->keys.erase(parent->keys.begin() + static_cast<std::ptrdiff_t>(node_idx));
            parent->children.erase(parent->children.begin() +
                                   static_cast<std::ptrdiff_t>(node_idx) + 1);

            if (parent != root_.get() && parent->keys.size() < get_min_keys()) {
                rebalance_after_delete(parent);
            }
        }
    }

    auto Btree::split_leaf(LeafNode *leaf) -> void {
        auto new_leaf = std::make_unique<LeafNode>();
        size_t split_idx = (max_degree_ + 1) / 2;
        auto split_offset = static_cast<std::ptrdiff_t>(split_idx);

        new_leaf->keys.assign(leaf->keys.begin() + split_offset, leaf->keys.end());
        new_leaf->values.assign(leaf->values.begin() + split_offset, leaf->values.end());

        leaf->keys.resize(split_idx);
        leaf->values.resize(split_idx);

        new_leaf->next = leaf->next;
        leaf->next = new_leaf.get();
        new_leaf->parent = leaf->parent;

        core::Key promote_key = new_leaf->keys.front();

        insert_into_parent(leaf, promote_key, std::move(new_leaf));
    }

    auto Btree::insert_into_parent(Node *old_child, const core::Key &key,
                                   std::unique_ptr<Node> new_child) -> void {
        if (old_child == root_.get()) {
            auto new_root = std::make_unique<InternalNode>();
            new_root->keys.push_back(key);

            new_root->children.push_back(std::move(root_));
            new_root->children.push_back(std::move(new_child));

            new_root->children[0]->parent = new_root.get();
            new_root->children[1]->parent = new_root.get();

            root_ = std::move(new_root);
            return;
        }

        auto *parent = static_cast<InternalNode *>(old_child->parent);

        auto it = std::upper_bound(parent->keys.begin(), parent->keys.end(), key);
        auto idx = std::distance(parent->keys.begin(), it);

        parent->keys.insert(it, key);
        parent->children.insert(parent->children.begin() + idx + 1, std::move(new_child));
        parent->children[static_cast<size_t>(idx + 1)]->parent = parent;

        if (parent->keys.size() >= max_degree_) {
            split_internal(parent);
        }
    }

    auto Btree::split_internal(InternalNode *node) -> void {
        auto new_sibling = std::make_unique<InternalNode>();
        size_t split_idx = (max_degree_ + 1) / 2;
        auto split_offset = static_cast<std::ptrdiff_t>(split_idx);

        core::Key promote_key = node->keys[split_idx];

        new_sibling->keys.assign(node->keys.begin() + split_offset + 1, node->keys.end());

        new_sibling->children.reserve(node->children.size() - split_idx - 1);
        for (auto it = node->children.begin() + split_offset + 1; it != node->children.end();
             ++it) {
            new_sibling->children.push_back(std::move(*it));
        }

        for (auto &child : new_sibling->children) {
            child->parent = new_sibling.get();
        }

        node->keys.resize(split_idx);
        node->children.resize(split_idx + 1);

        new_sibling->parent = node->parent;

        insert_into_parent(node, promote_key, std::move(new_sibling));
    }

    auto Btree::recover_from_wal() -> core::Status {
        if (wal_path_.empty()) {
            return core::Status::Ok();
        }

        recovering_ = true;

        struct RecoveryGuard {
            bool &flag;
            explicit RecoveryGuard(bool &f) : flag(f) {}
            ~RecoveryGuard() {
                flag = false;
            }
        };
        RecoveryGuard guard(recovering_);

        storage::WalReader reader(wal_path_);

        if (!reader.is_open()) {
            return core::Status::Ok();
        }

        size_t records_recovered = 0;
        storage::WalRecord record;

        while (reader.has_more()) {
            auto status = reader.read_next(record);
            if (status.is_not_found()) {
                break;
            }

            if (!status.ok()) {
                return status;
            }

            if (record.type == storage::WalRecordType::Put) {
                auto put_status = put(record.key, record.value);
                if (!put_status.ok()) {
                    return put_status;
                }
                records_recovered++;
            } else if (record.type == storage::WalRecordType::Delete) {
                auto delete_status = remove(record.key);
                if (!delete_status.ok() && !delete_status.is_not_found()) {
                    return delete_status;
                }
                records_recovered++;
            } else if (record.type == storage::WalRecordType::Update) {
                auto update_status = update(record.key, record.value);
                if (update_status.is_not_found()) {
                    fmt::print(
                        "WARN: UPDATE on missing key '{}' during recovery, treating as PUT\n",
                        record.key);

                    auto put_status = put(record.key, record.value);

                    if (!put_status.ok()) {
                        return put_status;
                    }
                } else if (!update_status.ok()) {
                    return update_status;
                }
                records_recovered++;
            }

            else if (record.type == storage::WalRecordType::Checkpoint) {
                // TODO : implement checkpointing
                fmt::print("INFO: Checkpoint marker found during recovery\n");
            }
        }

        fmt::print("Recovery complete: {} records replayed from WAL\n", records_recovered);
        return core::Status::Ok();
    }

    auto Btree::flush_wal() -> core::Status {
        if (wal_writer_) {
            return wal_writer_->sync();
        }
        return core::Status::Ok();
    }

    auto Btree::print_tree() -> void {
        std::vector<Node *> current_level;
        current_level.push_back(root_.get());

        while (!current_level.empty()) {
            std::vector<Node *> next_level;
            for (auto *node : current_level) {
                fmt::print("[ ");
                if (node->is_leaf()) {
                    auto *leaf = static_cast<LeafNode *>(node);
                    for (const auto &k : leaf->keys)
                        fmt::print("{} ", k);
                } else {
                    auto *internal = static_cast<InternalNode *>(node);
                    for (const auto &k : internal->keys)
                        fmt::print("{} ", k);

                    for (auto &child : internal->children)
                        next_level.push_back(child.get());
                }
                fmt::print("] ");
            }
            fmt::print("\n");
            current_level = next_level;
        }
    }
} // namespace embrace::indexing
