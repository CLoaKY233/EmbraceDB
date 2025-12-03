#include "core/common.hpp"
#include "indexing/btree.hpp"
#include "indexing/node.hpp"
#include <cstddef>
#include <fmt/core.h>
#include <memory>
#include <optional>
#include <vector>

namespace embrace::indexing {

    Btree::Btree() {
        root_ = std::make_unique<LeafNode>();
    }

    Btree::~Btree() = default;

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
            current = internal->children[static_cast<size_t>(idx)];
        }
        return static_cast<LeafNode *>(current);
    }

    auto Btree::put(const core::Key &key, const core::Value &value) -> core::Status {
        LeafNode *leaf = find_leaf(key);

        // check : key exists?
        int idx = leaf->get_index(key);
        if (idx != -1) {
            leaf->values[static_cast<size_t>(idx)] = value;
            return core::Status::Ok();
        }

        // Item sorted
        leaf->insert(key, value);

        // check for split
        if (leaf->keys.size() >= max_degree_) {
            split_leaf(leaf);
        }

        return core::Status::Ok();
    }

    auto Btree::split_leaf(LeafNode *leaf) -> void {
        // 1. Create new sibling leaf
        auto *new_leaf = new LeafNode();
        size_t split_idx = (max_degree_ + 1) / 2;

        auto split_offset = static_cast<std::ptrdiff_t>(split_idx);

        // 2. Move second half of the keys/values too new leaf
        new_leaf->keys.assign(leaf->keys.begin() + split_offset, leaf->keys.end());
        new_leaf->values.assign(leaf->values.begin() + split_offset, leaf->values.end());

        // 3. Resize the original leaf
        leaf->keys.resize(split_idx);
        leaf->values.resize(split_idx);

        // 4. Link siblings
        new_leaf->next = leaf->next;
        leaf->next = new_leaf;
        new_leaf->parent = leaf->parent;

        // 5. Propogate up
        core::Key promote_key = new_leaf->keys.front();
        insert_into_parent(leaf, promote_key, new_leaf);
    }

    auto Btree::insert_into_parent(Node *old_child, const core::Key &key, Node *new_child) -> void {
        // Case1. Root split
        if (old_child == root_.get()) {
            Node *old_root_raw = root_.release();

            auto new_root = std::make_unique<InternalNode>();
            new_root->keys.push_back(key);
            new_root->children.push_back(old_child);
            new_root->children.push_back(new_child);

            old_root_raw->parent = new_root.get();
            new_child->parent = new_root.get();

            // Release ownership of old root unique ptr and transfer to new root
            // TODO : handle this better
            root_ = std::move(new_root);

            return;
        }

        // Case2. Insert into existing parent
        auto *parent = static_cast<InternalNode *>(old_child->parent);

        auto it = std::upper_bound(parent->keys.begin(), parent->keys.end(), key);
        auto idx = std::distance(parent->keys.begin(), it);

        parent->keys.insert(it, key);
        parent->children.insert(parent->children.begin() + idx + 1, new_child);

        new_child->parent = parent;

        if (parent->keys.size() >= max_degree_) {
            fmt::print("WARNING: Internal node split required but not implemented in Sprint 0!\n");
            // TODO : Implement internal node splitting
        }
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
                    for (auto *child : internal->children)
                        next_level.push_back(child);
                }
                fmt::print("] ");
            }
            fmt::print("\n");
            current_level = next_level;
        }
    }

} // namespace embrace::indexing
