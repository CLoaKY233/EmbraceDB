#pragma once

#include "core/common.hpp"
#include "core/status.hpp"
#include "indexing/node.hpp"
#include "storage/wal.hpp"
#include <memory>
#include <optional>
#include <string>

namespace embrace::indexing {

    class Btree {
      public:
        explicit Btree(const std::string &wal_path = "");
        ~Btree();

        [[nodiscard]] auto get(const core::Key &key) -> std::optional<core::Value>;
        [[nodiscard]] auto put(const core::Key &key, const core::Value &value) -> core::Status;

        auto recover_from_wal() -> core::Status;
        auto flush_wal() -> core::Status;

        // DEBUG
        auto print_tree() -> void;

      private:
        std::unique_ptr<Node> root_;
        std::unique_ptr<storage::WalWriter> wal_writer_;
        std::string wal_path_;

        bool recovering_;

        // Configuration
        const size_t max_degree_ = 4;

        // Internal helpers
        auto find_leaf(const core::Key &key) -> LeafNode *;
        auto split_leaf(LeafNode *leaf) -> void;
        auto split_internal(InternalNode *node) -> void;

        auto insert_into_parent(Node *old_child, const core::Key &key,
                                std::unique_ptr<Node> new_child) -> void;
    };

} // namespace embrace::indexing
