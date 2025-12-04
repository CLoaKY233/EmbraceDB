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
                wal_writer_.reset(); // Clear the writer
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
        // new_leaf is now nullptr - ownership transferred!
    }

    auto Btree::insert_into_parent(Node *old_child, const core::Key &key,
                                   std::unique_ptr<Node> new_child) -> void {
        // Case 1: Root split
        if (old_child == root_.get()) {
            auto new_root = std::make_unique<InternalNode>();
            new_root->keys.push_back(key);

            new_root->children.push_back(std::move(root_));
            new_root->children.push_back(std::move(new_child));

            // Update parent pointers
            new_root->children[0]->parent = new_root.get();
            new_root->children[1]->parent = new_root.get();

            root_ = std::move(new_root);
            return;
        }

        // Case 2: Insert into existing parent
        auto *parent = static_cast<InternalNode *>(old_child->parent);

        auto it = std::upper_bound(parent->keys.begin(), parent->keys.end(), key);
        auto idx = std::distance(parent->keys.begin(), it);

        parent->keys.insert(it, key);

        parent->children.insert(parent->children.begin() + idx + 1, std::move(new_child));

        // Update parent pointer
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

        // Copy keys to new sibling
        new_sibling->keys.assign(node->keys.begin() + split_offset + 1, node->keys.end());

        new_sibling->children.reserve(node->children.size() - split_idx - 1);
        for (auto it = node->children.begin() + split_offset + 1; it != node->children.end();
             ++it) {
            new_sibling->children.push_back(std::move(*it));
        }

        // Update parent pointers
        for (auto &child : new_sibling->children) {
            child->parent = new_sibling.get();
        }

        // Resize original node
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
