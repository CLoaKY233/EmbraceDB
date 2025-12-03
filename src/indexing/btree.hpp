#pragma once

#include "core/common.hpp"
#include "core/status.hpp"
#include "indexing/node.hpp"
#include <memory>
#include <optional>

namespace embrace::indexing {

    class Btree {
      public:
        Btree();
        ~Btree();

        [[nodiscard]] auto get(const core::Key &key) -> std::optional<core::Value>;
        [[nodiscard]] auto put(const core::Key &key, const core::Value &value) -> core::Status;

        // debug helper
        auto print_tree() -> void;

      private:
        std::unique_ptr<Node> root_;

        // Configuration
        const size_t max_degree_ = 4; // TODO : increase size, intially small for debugging

        // Internal helpers
        auto find_leaf(const core::Key &key) -> LeafNode *;
        auto split_leaf(LeafNode *leaf) -> void;
        auto split_internal(InternalNode *node) -> void;
        auto insert_into_parent(Node *old_child, const core::Key &key, Node *new_child) -> void;
    };

} // namespace embrace::indexing
