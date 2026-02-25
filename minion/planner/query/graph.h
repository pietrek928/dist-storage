#pragma once

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>
#include <string>
#include <map>

#include <data.pb.h>
#include <query.pb.h>


using node_t = int;

typedef struct {
    query::ColumnOperation op;
    node_t col;
    node_t df;
    data::ValueType type;
    bool array;
} Operation;

typedef struct {
    std::vector<int64_t> int64_constants;
    std::vector<std::vector<int64_t>> int64_array_constants;
    std::vector<double> float64_constants;
    std::vector<std::vector<double>> float64_array_constants;
    std::vector<std::string> string_constants;
    std::vector<std::vector<std::string>> string_array_constants;
    // TODO: handle JSON

    std::vector<std::string> column_names;
    std::vector<std::string> df_names = {"const"};
    std::vector<Operation> operations;
} QueryGraph;


node_t appendInt64Constant(QueryGraph &graph, const std::string &name, int64_t value) {
    node_t const_id = graph.int64_constants.size();
    graph.int64_constants.push_back(value);
    graph.column_names.push_back(name);
    graph.operations.emplace_back(Operation{
        .op = query::ColumnOperation::CONST,
        .col = const_id,
        .df = 0,  // df=0 -> constant
        .type = data::ValueType::Int64,
        .array = false,
    });
    return const_id;
}

node_t appendFloat64Constant(QueryGraph &graph, const std::string &name, double value) {
    node_t const_id = graph.float64_constants.size();
    graph.float64_constants.push_back(value);
    graph.column_names.push_back(name);
    graph.operations.emplace_back(Operation{
        .op = query::ColumnOperation::CONST,
        .col = const_id,
        .df = 0,  // df=0 -> constant
        .type = data::ValueType::Float64,
        .array = false,
    });
    return const_id;
}

template<typename Tvalue>
Tvalue getNumericConstant(const QueryGraph &graph, const Operation &const_op) {
    auto const_num = const_op.col;
    switch (const_op.type) {
        case data::ValueType::Int64:
            if (const_op.array) {
                if (const_num >= graph.int64_array_constants.size()) {
                    throw std::invalid_argument("Invalid int64 array constant id " + std::to_string(const_num));
                }
                auto &array = graph.int64_array_constants[const_num];
                if (array.size() != 1) {
                    throw std::invalid_argument("Invalid int64 array length=" + std::to_string(array.size()) + "constant id " + std::to_string(const_num));
                }
                return array[0];
            } else {
                if (const_num >= graph.int64_constants.size()) {
                    throw std::invalid_argument("Invalid int64 constant id " + std::to_string(const_num));
                }
                return graph.int64_constants[const_num];
            }
        case data::ValueType::Float64:
            if (const_op.array) {
                if (const_num >= graph.float64_array_constants.size()) {
                    throw std::invalid_argument("Invalid float64 array constant id " + std::to_string(const_num));
                }
                auto &array = graph.float64_array_constants[const_num];
                if (array.size() != 1) {
                    throw std::invalid_argument("Invalid float64 constant id " + std::to_string(const_num));
                }
                return array[0];
            } else {
                if (const_num >= graph.float64_constants.size()) {
                    throw std::invalid_argument("Invalid float64 constant id " + std::to_string(const_num));
                    }
                return graph.float64_constants[const_num];
            }
        default:
            throw std::invalid_argument("Invalid constant id " + std::to_string(const_num) + " type " + std::to_string(const_op.type));
    }
}

std::vector<std::vector<node_t>> getGraphMapping(
    const std::vector<std::pair<node_t, node_t>> &edges
) {
    node_t n = 0;
    for (const auto &edge : edges) {
        n = std::max(n, edge.first);
        n = std::max(n, edge.second);
    }
    n++;
    std::vector<std::vector<node_t>> graph_mapping(n);
    for (const auto &edge : edges) {
        graph_mapping[edge.first].push_back(edge.second);
    }
    return graph_mapping;
}

std::vector<std::pair<node_t, node_t>> getEdgesList(
    const std::vector<std::vector<node_t>> &graph_mapping
) {
    node_t l = 0;
    for (const auto &vec : graph_mapping) {
        l += vec.size();
    }

    std::vector<std::pair<node_t, node_t>> edges(l);
    l = 0;
    for (node_t i = 0; i < graph_mapping.size(); i++) {
        for (const auto &j : graph_mapping[i]) {
            edges[l++] = std::make_pair(i, j);
        }
    }
    return edges;
}

query::ColumnOperation getNegatedOperation(
    query::ColumnOperation op
) {
    switch (op) {
        case query::ColumnOperation::NOT:
            return query::ColumnOperation::NOP;
        case query::ColumnOperation::AND:
            return query::ColumnOperation::NAND;
        case query::ColumnOperation::OR:
            return query::ColumnOperation::NOR;
        case query::ColumnOperation::XOR:
            return query::ColumnOperation::XNOR;
        case query::ColumnOperation::NAND:
            return query::ColumnOperation::AND;
        case query::ColumnOperation::NOR:
            return query::ColumnOperation::OR;
        case query::ColumnOperation::XNOR:
            return query::ColumnOperation::XOR;
        case query::ColumnOperation::GT:
            return query::ColumnOperation::LTE;
        case query::ColumnOperation::LT:
            return query::ColumnOperation::GTE;
        case query::ColumnOperation::GTE:
            return query::ColumnOperation::LT;
        case query::ColumnOperation::LTE:
            return query::ColumnOperation::GT;
        case query::ColumnOperation::EQ:
            return query::ColumnOperation::NEQ;
        case query::ColumnOperation::NEQ:
            return query::ColumnOperation::EQ;
        case query::ColumnOperation::IN:
            return query::ColumnOperation::NOT_IN;
        case query::ColumnOperation::NOT_IN:
            return query::ColumnOperation::IN;
        case query::ColumnOperation::LIKE:
            return query::ColumnOperation::NOT_LIKE;
        case query::ColumnOperation::NOT_LIKE:
            return query::ColumnOperation::LIKE;
        case query::ColumnOperation::ILIKE:
            return query::ColumnOperation::NOT_ILIKE;
        case query::ColumnOperation::NOT_ILIKE:
            return query::ColumnOperation::ILIKE;
        case query::ColumnOperation::IS_NULL:
            return query::ColumnOperation::IS_NOT_NULL;
        case query::ColumnOperation::IS_NOT_NULL:
            return query::ColumnOperation::IS_NULL;
        case query::ColumnOperation::BETWEEN:
            return query::ColumnOperation::NOT_BETWEEN;
        case query::ColumnOperation::NOT_BETWEEN:
            return query::ColumnOperation::BETWEEN;

        default:
            return query::ColumnOperation::INVALID;
    }
}

void fillVisited(
    const std::vector<std::vector<node_t>> &graph_mapping,
    std::vector<node_t> &visited,
    node_t node, node_t value
) {
    visited[node] = value;
    for (const auto &child : graph_mapping[node]) {
        if (!visited[child]) {
            fillVisited(graph_mapping, visited, child, value+1);
        }
    }
}

std::map<node_t, node_t> visitedLeaveMap(
    const std::vector<node_t> &visited
) {
    std::vector<node_t> visitedNodes;
    for (node_t i = 0; i < visited.size(); i++) {
        if (visited[i]) {
            visitedNodes.push_back(i);
        }
    }
    std::sort(
        visitedNodes.begin(), visitedNodes.end(),
        [&visited](node_t a, node_t b) {
            return visited[a] > visited[b];
        }
    );
    std::map<node_t, node_t> newMapping;
    for (node_t i = 0; i < visitedNodes.size(); i++) {
        newMapping[visitedNodes[i]] = i;
    }
    return newMapping;
}

std::vector<std::vector<node_t>> mapMappingGraph(
    const std::vector<std::vector<node_t>> &graph_mapping,
    const std::map<node_t, node_t> &newMapping
) {
    std::vector<std::vector<node_t>> newGraphMapping(newMapping.size());
    for (node_t node1 = 0; node1 < graph_mapping.size(); node1++) {
        const auto node1_iter = newMapping.find(node1);
        if (node1_iter == newMapping.end()) {
            continue;
        }
        auto node1_mapped = node1_iter->second;

        const auto &vec = graph_mapping[node1];
        for (const auto &node2 : vec) {
            const auto node2_iter = newMapping.find(node2);
            if (node2_iter == newMapping.end()) {
                continue;
            }
            auto node2_mapped = node2_iter->second;
            newGraphMapping[node1_mapped].push_back(node2_mapped);
        }
    }
    return newGraphMapping;
}

std::vector<Operation> mapOperations(
    const std::vector<Operation> &operations,
    const std::map<node_t, node_t> &newMapping
) {
    std::vector<Operation> newOperations(newMapping.size());
    for (const auto &m : newMapping) {
        newOperations[m.second] = operations[m.first];
    }
    return newOperations;
}
