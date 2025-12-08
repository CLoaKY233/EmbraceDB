#pragma once

#include "core/common.hpp"
#include "core/status.hpp"
#include "indexing/node.hpp"
#include "storage/snapshot.hpp"
#include "storage/wal.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace embrace::indexing {

    class Btree {
      public:
        explicit Btree(const std::string &wal_path = "");
        ~Btree();

        // CORE OPS
        [[nodiscard]] auto get(const core::Key &key) -> std::optional<core::Value>;
        [[nodiscard]] auto put(const core::Key &key, const core::Value &value) -> core::Status;
        [[nodiscard]] auto update(const core::Key &key, const core::Value &value) -> core::Status;
        [[nodiscard]] auto remove(const core::Key &key) -> core::Status;

        auto recover_from_wal() -> core::Status;
        auto flush_wal() -> core::Status;
        auto create_checkpoint() -> core::Status;
        void set_checkpoint_interval(size_t interval) {
            checkpoint_interval_ = interval;
        }

        auto iterate_all(std::function<void(const core::Key &, const core::Value &)> callback) const
            -> void;

        // DEBUG
        auto print_tree() -> void;

      private:
        std::unique_ptr<Node> root_;
        std::unique_ptr<storage::WalWriter> wal_writer_;
        std::string wal_path_;

        bool recovering_;

        std::unique_ptr<storage::Snapshotter> snapshotter_;
        size_t operation_count_ = 0;
        size_t checkpoint_interval_ = 10000;

        // Configuration
        const size_t max_degree_ = 4;

        // Internal helpers
        auto find_leftmost_leaf() const -> LeafNode *;
        auto find_leaf(const core::Key &key) -> LeafNode *;
        auto split_leaf(LeafNode *leaf) -> void;
        auto split_internal(InternalNode *node) -> void;

        auto insert_into_parent(Node *old_child, const core::Key &key,
                                std::unique_ptr<Node> new_child) -> void;

        auto borrow_from_left(LeafNode *node, LeafNode *left_sibling, InternalNode *parent,
                              size_t parent_key_idx) -> void;
        auto borrow_from_right(LeafNode *node, LeafNode *right_sibling, InternalNode *parent,
                               size_t parent_key_idx) -> void;

        auto merge_with_left(LeafNode *node, LeafNode *left_sibling, InternalNode *parent,
                             size_t parent_key_idx) -> void;
        auto merge_with_right(LeafNode *node, LeafNode *right_sibling, InternalNode *parent,
                              size_t parent_key_idx) -> void;

        auto rebalance_after_delete(Node *node) -> void;
        auto handle_underflow_internal(InternalNode *node) -> void;
        [[nodiscard]] auto get_min_keys() const -> size_t {
            return (max_degree_ + 1) / 2;
        }
    };

} // namespace embrace::indexing
