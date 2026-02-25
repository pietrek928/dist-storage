#pragma once

#include <vector>

#include <query.pb.h>

#include "graph.h"


node_t clone_node(
    std::vector<std::vector<node_t>> &graph_mapping,
    std::vector<Operation> &operations,
    node_t node
) {
    node_t new_node_idx = operations.size();
    operations.push_back(operations[node]);
    graph_mapping.push_back(graph_mapping[node]);
    return new_node_idx;
}

bool roll_constants_applies(
    const std::vector<std::vector<node_t>> &graph_mapping,
    const std::vector<Operation> &operations,
    node_t node
) {
    if (operations[node].op != query::ColumnOperation::CONST) {
        return false;
    }
    if (!graph_mapping[node].size()) {
        return false;
    }

    for (const auto &child : graph_mapping[node]) {
        if (
            operations[child].op != query::ColumnOperation::CONST
            || graph_mapping[child].size()
        ) {
            return false;
        }
    }

    return true;
}

node_t demorgan_apply(
    std::vector<std::vector<node_t>> &graph_mapping,
    std::vector<Operation> &operations,
    node_t node
) {
    node_t new_node_idx = clone_node(graph_mapping, operations, node);
    auto &new_node = operations[new_node_idx];
    switch (new_node.op) {
        case query::ColumnOperation::AND:
            new_node.op = query::ColumnOperation::NOR;
            break;
        case query::ColumnOperation::OR:
            new_node.op = query::ColumnOperation::NAND;
            break;
        case query::ColumnOperation::NAND:
            new_node.op = query::ColumnOperation::OR;
            break;
        case query::ColumnOperation::NOR:
            new_node.op = query::ColumnOperation::AND;
            break;
        default:
            throw std::invalid_argument("Invalid operation for demorgan: " + std::to_string(new_node.op));
    }

    auto old_children = graph_mapping[node];
    graph_mapping[new_node_idx].clear();
    for (const auto &child : old_children) {
        auto new_child_idx = clone_node(graph_mapping, operations, child);
        auto &new_child = operations[new_child_idx];

        auto negated_op = getNegatedOperation(new_child.op);
        if (negated_op == query::ColumnOperation::INVALID) {
            throw std::invalid_argument("Invalid operation for negation: " + std::to_string(new_child.op));
        }
        new_child.op = negated_op;
        graph_mapping[new_node_idx].push_back(new_child_idx);
    }
    return new_node_idx;
}

node_t merge_cummutative_numeric_apply(
    std::vector<std::vector<node_t>> &graph_mapping,
    std::vector<Operation> &operations,
    node_t node
) {
    node_t new_node_idx = clone_node(graph_mapping, operations, node);
    auto node_op = operations[node].op;

    auto old_children = graph_mapping[new_node_idx];
    graph_mapping[new_node_idx].clear();
    for (auto child : old_children) {
        auto child_op = operations[child].op;
        if (child_op == node_op) {
            const auto &grand_children = graph_mapping[child];
            graph_mapping[new_node_idx].insert(graph_mapping[new_node_idx].end(), grand_children.begin(), grand_children.end());
        } else {
            graph_mapping[new_node_idx].push_back(child);
        }
    }
    return new_node_idx;
}

node_t merge_cummutative_logic_apply(
    std::vector<std::vector<node_t>> &graph_mapping,
    std::vector<Operation> &operations,
    node_t node
) {
    node_t new_node_idx = clone_node(graph_mapping, operations, node);
    auto node_op = operations[node].op;
    switch(node_op) {
        case query::ColumnOperation::AND:
        case query::ColumnOperation::OR:
        case query::ColumnOperation::XOR:
            break;
        case query::ColumnOperation::NAND:
            node_op = query::ColumnOperation::AND;
            break;
        case query::ColumnOperation::NOR:
            node_op = query::ColumnOperation::OR;
            break;
        case query::ColumnOperation::XNOR:
            node_op = query::ColumnOperation::XOR;
            break;
        default:
            throw std::invalid_argument("Invalid operation for merge cummutative logic: " + std::to_string(node_op));
    }

    auto old_children = graph_mapping[new_node_idx];
    graph_mapping[new_node_idx].clear();
    for (auto child : old_children) {
        auto child_op = operations[child].op;
        if (child_op == node_op) {
            const auto &grand_children = graph_mapping[child];
            graph_mapping[new_node_idx].insert(graph_mapping[new_node_idx].end(), grand_children.begin(), grand_children.end());
        } else {
            graph_mapping[new_node_idx].push_back(child);
        }
    }
    return new_node_idx;
}

node_t reduce_not_nop_apply(
    std::vector<std::vector<node_t>> &graph_mapping,
    std::vector<Operation> &operations,
    node_t node
) {
    bool negate;
    switch (operations[node].op) {
        case query::ColumnOperation::NOT:
            negate = true;
            break;
        case query::ColumnOperation::NOP:
            negate = false;
            break;
        default:
            throw std::invalid_argument("Invalid operation for reduce not nop: " + std::to_string(operations[node].op));
    }

    const auto &children = graph_mapping[node];
    if (children.size() != 1) {
        throw std::invalid_argument("Invalid number of children for reduce not nop: " + std::to_string(children.size()));
    }
    if (!negate) {
        return children[0];
    }

    node_t new_child_idx = clone_node(graph_mapping, operations, children[0]);
    auto &new_child = operations[new_child_idx];

    auto negated_op = getNegatedOperation(new_child.op);
    if (negated_op == query::ColumnOperation::INVALID) {
        throw std::invalid_argument("Invalid operation for negation: " + std::to_string(new_child.op));
    }
    new_child.op = negated_op;

    return new_child_idx;
}

bool pull_df_applies(
    const std::vector<std::vector<node_t>> &graph_mapping,
    const std::vector<Operation> &operations,
    node_t node
) {
    auto node_df = operations[node].df;
    if (node_df == 0) {
        return false;
    }

    node_t children_common_df = 0;
    for (const auto &child : graph_mapping[node]) {
        auto child_df = operations[child].df;
        if (child_df && child_df != children_common_df) {
            if (children_common_df) {
                return false;
            } else {
                children_common_df = child_df;
            }
        }
    }

    return children_common_df && children_common_df != node_df;
}

node_t pull_df_apply(
    std::vector<std::vector<node_t>> &graph_mapping,
    std::vector<Operation> &operations,
    node_t node
) {
    auto node_df = operations[node].df;
    if (node_df == 0) {
        throw std::invalid_argument("Invalid df for pull df: " + std::to_string(node_df));
    }

    node_t new_node_idx = clone_node(graph_mapping, operations, node);
    node_t children_common_df = 0;
    for (const auto &child : graph_mapping[new_node_idx]) {
        auto child_df = operations[child].df;
        if (child_df) {
            children_common_df = child_df;
            break;
        }
    }

    operations[new_node_idx].df = children_common_df;
    return new_node_idx;
}
