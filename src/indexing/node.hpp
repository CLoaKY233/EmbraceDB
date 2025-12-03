#pragma once

#include "core/common.hpp"
#include <algorithm>
#include <iterator>
#include <vector>

namespace embrace::indexing {

    enum class NodeType { Internal, Leaf };

    struct Node {
        NodeType type;
        Node *parent = nullptr;

        explicit Node(NodeType t) : type(t) {}
        virtual ~Node() = default;

        [[nodiscard]] auto is_leaf() const -> bool {
            return type == NodeType::Leaf;
        }
    };

    struct LeafNode : public Node {
        std::vector<core::Key> keys;
        std::vector<core::Value> values;

        LeafNode *next = nullptr;
        LeafNode *prev = nullptr;

        LeafNode() : Node(NodeType::Leaf) {
            // Reserve space to avoid reallocation during initial fills
            keys.reserve(32);
            values.reserve(32);
        }

        auto get_index(const core::Key &key) -> int {
            // std::lower_bound performs binary search O(log N)
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
        std::vector<Node *> children;

        InternalNode() : Node(NodeType::Internal) {}
    };

} // namespace embrace::indexing
