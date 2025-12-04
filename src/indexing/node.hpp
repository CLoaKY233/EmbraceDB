#pragma once

#include "core/common.hpp"
#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>

namespace embrace::indexing {

    enum class NodeType { Internal, Leaf };

    struct Node {
        NodeType type;
        Node *parent = nullptr; // Non-owning pointer (parent owns us)

        explicit Node(NodeType t) : type(t) {}
        virtual ~Node() = default;

        [[nodiscard]] auto is_leaf() const -> bool {
            return type == NodeType::Leaf;
        }
    };

    struct LeafNode : public Node {
        std::vector<core::Key> keys;
        std::vector<core::Value> values;

        LeafNode *next = nullptr; // Non-owning pointer (tree manages ownership)
        LeafNode *prev = nullptr; // Non-owning pointer

        LeafNode() : Node(NodeType::Leaf) {
            keys.reserve(32);
            values.reserve(32);
        }

        ~LeafNode() override = default;

        auto get_index(const core::Key &key) -> int {
            auto it = std::lower_bound(keys.begin(), keys.end(), key);
            if (it != keys.end() && *it == key) {
                return static_cast<int>(std::distance(keys.begin(), it));
            }
            return -1;
        }

        auto insert(const core::Key &key, const core::Value &val) -> void {
            auto it = std::lower_bound(keys.begin(), keys.end(), key);
            auto idx = std::distance(keys.begin(), it);

            keys.insert(it, key);
            values.insert(values.begin() + idx, val);
        }
    };

    struct InternalNode : public Node {
        std::vector<core::Key> keys;

        std::vector<std::unique_ptr<Node>> children;

        InternalNode() : Node(NodeType::Internal) {}

        ~InternalNode() override = default;
    };

} // namespace embrace::indexing
