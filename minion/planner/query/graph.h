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
using node_hash_t = uint64_t;
constexpr node_t NODE_NONE = -1;


typedef struct Operation {
    query::ColumnOperation op;
    node_t name = NODE_NONE;
    node_t const_id = NODE_NONE;
    node_t df = NODE_NONE;
    data::ValueType type = data::ValueType::Any;
    data::SequenceType sequence;
} Operation;

typedef struct QueryGraph {
    std::vector<int64_t> int64_constants;
    std::vector<std::vector<int64_t>> int64_array_constants;
    std::vector<double> float64_constants;
    std::vector<std::vector<double>> float64_array_constants;
    std::vector<std::string> string_constants;
    std::vector<std::vector<std::string>> string_array_constants;
    // TODO: handle JSON

    std::vector<std::string> names;
    std::vector<Operation> operations;

    std::vector<std::vector<node_t>> graph_mapping;
} QueryGraph;


node_t appendName(QueryGraph &graph, const std::string &name) {
    node_t name_id = graph.names.size();
    graph.names.push_back(name);
    return name_id;
}

void setNodeName(QueryGraph &graph, node_t node_id, const std::string &name) {
    node_t name_id = appendName(graph, name);
    graph.operations[node_id].name = name_id;
}

std::string getNodeName(const QueryGraph &graph, node_t node_id) {
    node_t name_id = graph.operations[node_id].name;
    if (name_id == NODE_NONE) {
        return "";
    }
    return graph.names[name_id];
}


node_t appendInt64Constant(QueryGraph &graph, int64_t value) {
    node_t const_id = graph.int64_constants.size();
    graph.int64_constants.push_back(value);
    node_t node_id = graph.operations.size();
    graph.graph_mapping.emplace_back();
    graph.operations.emplace_back(Operation{
        .op = query::ColumnOperation::CONST,
        .const_id = const_id,
        .df = 0,  // df=0 -> constant
        .type = data::ValueType::Int64,
        .sequence = data::SequenceType::SINGLE,
    });
    return node_id;
}

node_t appendFloat64Constant(QueryGraph &graph, double value) {
    node_t const_id = graph.float64_constants.size();
    graph.float64_constants.push_back(value);
    node_t node_id = graph.operations.size();
    graph.graph_mapping.emplace_back();
    graph.operations.emplace_back(Operation{
        .op = query::ColumnOperation::CONST,
        .const_id = const_id,
        .df = 0,  // df=0 -> constant
        .type = data::ValueType::Float64,
        .sequence = data::SequenceType::SINGLE,
    });
    return node_id;
}

node_t appendStringConstant(QueryGraph &graph, const std::string& value) {
    node_t const_id = graph.string_constants.size();
    graph.string_constants.push_back(value);
    node_t node_id = graph.operations.size();
    graph.graph_mapping.emplace_back();
    graph.operations.emplace_back(Operation{
        .op = query::ColumnOperation::CONST,
        .const_id = const_id,
        .df = 0,  // df=0 -> constant
        .type = data::ValueType::String,
        .sequence = data::SequenceType::SINGLE,
    });
    return node_id;
}

node_t appendColumn(QueryGraph &graph, const std::string &name, data::ValueType type) {
    node_t name_id = appendName(graph, name);
    node_t node_id = graph.operations.size();
    graph.graph_mapping.emplace_back();
    graph.operations.emplace_back(Operation{
        .op = query::ColumnOperation::COLUMN,
        .name = name_id,
        .type = type,
        .sequence = data::SequenceType::ARRAY,
    });
    return node_id;
}

node_t appendTable(
    QueryGraph &graph,
    const std::string &name,
    const std::vector<node_t> &col_node_ids
) {
    node_t name_id = appendName(graph, name);
    node_t node_id = graph.operations.size();
    graph.graph_mapping.emplace_back(col_node_ids);
    graph.operations.emplace_back(Operation{
        .op = query::ColumnOperation::TABLE,
        .name = name_id,
        .sequence = data::SequenceType::ARRAY,
    });
    return node_id;
}

void loadTableNames(
    std::map<std::string, node_t> &ctx,
    const QueryGraph &graph, node_t table_node_id, std::string table_name = ""
) {
    for (const auto &col_node_id : graph.graph_mapping[table_node_id]) {
        node_t name_id = graph.operations[col_node_id].name;
        if (name_id != NODE_NONE) {
            ctx[graph.names[name_id]] = col_node_id;
            if (!table_name.empty()) {
                ctx[table_name + "." + graph.names[name_id]] = col_node_id;
            }
        }
    }
}

node_t appendNode(
    QueryGraph &graph, node_t df, query::ColumnOperation op, data::ValueType type,
    const std::vector<node_t> args = {}, data::SequenceType sequence = data::SequenceType::SINGLE
) {
    node_t node_id = graph.operations.size();
    graph.graph_mapping.emplace_back(args);
    graph.operations.emplace_back(Operation{
        .op = op,
        .df = df,
        .type = type,
        .sequence = data::SequenceType::SINGLE,
    });
    return node_id;
}

template<typename Tvalue>
Tvalue getNumericConstant(const QueryGraph &graph, const Operation &const_op) {
    auto const_num = const_op.const_id;
    switch (const_op.type) {
        case data::ValueType::Int64:
            if (const_op.sequence == data::SequenceType::ARRAY) {
                if (const_num < 0 || const_num >= graph.int64_array_constants.size()) {
                    throw std::invalid_argument("Invalid int64 array constant id " + std::to_string(const_num));
                }
                auto &array = graph.int64_array_constants[const_num];
                if (array.size() != 1) {
                    throw std::invalid_argument("Invalid int64 array length=" + std::to_string(array.size()) + "constant id " + std::to_string(const_num));
                }
                return array[0];
            } else {
                if (const_num < 0 || const_num >= graph.int64_constants.size()) {
                    throw std::invalid_argument("Invalid int64 constant id " + std::to_string(const_num));
                }
                return graph.int64_constants[const_num];
            }
        case data::ValueType::Float64:
            if (const_op.sequence == data::SequenceType::ARRAY) {
                if (const_num < 0 || const_num >= graph.float64_array_constants.size()) {
                    throw std::invalid_argument("Invalid float64 array constant id " + std::to_string(const_num));
                }
                auto &array = graph.float64_array_constants[const_num];
                if (array.size() != 1) {
                    throw std::invalid_argument("Invalid float64 constant id " + std::to_string(const_num));
                }
                return array[0];
            } else {
                if (const_num < 0 || const_num >= graph.float64_constants.size()) {
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

std::vector<std::vector<node_t>> revertGraphMapping(
    const std::vector<std::vector<node_t>> &graph_mapping
) {
    std::vector<std::vector<node_t>> resersed(graph_mapping.size());
    for (node_t i = 0; i < graph_mapping.size(); i++) {
        for (const auto &j : graph_mapping[i]) {
            resersed[j].push_back(i);
        }
    }
    return resersed;
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
        case query::ColumnOperation::NULL_:
            return query::ColumnOperation::NULL_;
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

        default:
            return query::ColumnOperation::INVALID;
    }
}

bool isCummutative(
    query::ColumnOperation op
) {
    switch (op) {
        case query::ColumnOperation::AND:
        case query::ColumnOperation::NAND:
        case query::ColumnOperation::OR:
        case query::ColumnOperation::NOR:
        case query::ColumnOperation::XOR:
        case query::ColumnOperation::XNOR:
        case query::ColumnOperation::ADD:
        case query::ColumnOperation::MUL:
        case query::ColumnOperation::MIN:
        case query::ColumnOperation::MAX:
        case query::ColumnOperation::AVG:
        case query::ColumnOperation::COUNT:
        case query::ColumnOperation::COUNT_DISTINCT:
        case query::ColumnOperation::SUM:
        case query::ColumnOperation::MEDIAN:
        case query::ColumnOperation::VAR:
        case query::ColumnOperation::STD:
            return true;
        default:
            return false;
    }
}

bool isTableOperation(
    query::ColumnOperation op
) {
    switch (op) {
        case query::ColumnOperation::TABLE:
        case query::ColumnOperation::DF:
        case query::ColumnOperation::FILTER:
        case query::ColumnOperation::RANGE:
        case query::ColumnOperation::SORT:
        case query::ColumnOperation::UNION:
            return true;
        default:
            return false;
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

std::vector<node_t> visitedNodesOrder(
    const std::vector<node_t> &visited
) {
    std::vector<node_t> nodesOrder;
    for (node_t i = 0; i < visited.size(); i++) {
        if (visited[i]) {
            nodesOrder.push_back(i);
        }
    }
    std::sort(
        nodesOrder.begin(), nodesOrder.end(),
        [&visited](node_t a, node_t b) {
            return visited[a] > visited[b];
        }
    );
    return nodesOrder;
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

void fillDFIdsWalk(
    std::vector<Operation> &operations,
    const std::vector<std::vector<node_t>> &graph_mapping,
    node_t df_node_id
) {
    auto &node = operations[df_node_id];
    if (node.df != NODE_NONE) {
        return;
    }
    node.df = df_node_id;
    for (auto child_id : graph_mapping[df_node_id]) {
        const auto &child_node = operations[child_id];
        if (!isTableOperation(child_node.op)) {
            fillDFIdsWalk(operations, graph_mapping, child_id);
        }
    }
}

// Assume nodes are in parsing order
void fillDFIds(
    std::vector<Operation> &operations,
    const std::vector<std::vector<node_t>> &graph_mapping
) {
    for (node_t i = 0; i < operations.size(); i++) {
        if (isTableOperation(operations[i].op)) {
            fillDFIdsWalk(operations, graph_mapping, i);
        }
    }
}

node_hash_t hashChildrenPositional(
    const node_t *children, int nchildren,
    const std::vector<node_hash_t> &nodeHashes
) {
    node_hash_t hash = 583747;
    for (int i = 0; i < nchildren; i++) {
        hash = (hash + nodeHashes[children[i]] + 82937) * 4565;
    }
    return hash;
}

node_hash_t hashChildrenSet(
    const node_t *chindren, int nchildren,
    const std::vector<node_hash_t> &nodeHashes
) {
    node_hash_t hash = 583747;
    for (int i = 0; i < nchildren; i++) {
        hash *= hash + 989673;
    }
    return hash;
}

node_hash_t hashOperationParams(const Operation &op) {
    node_hash_t hash = 45643;
    hash = hash * 5667 + op.op;
    hash = hash * 5453 + op.name;
    hash = hash * 4567 + op.const_id;
    hash = hash * 6357 + op.df;
    hash = hash * 8763 + op.type;
    hash = hash * 5 + op.sequence * 3;
    return hash;
}

void fillNodeHashes(
    const std::vector<std::vector<node_t>> &graph_mapping,
    const std::vector<Operation> &operations,
    const std::vector<node_t> &nodesOrder,
    std::vector<node_hash_t> &nodeHashes
) {
    for (const auto &node : nodesOrder) {
        const auto &vec = graph_mapping[node];
        node_hash_t child_hash = 0;
        if (!vec.empty()) {
            if (!isCummutative(operations[node].op)) {
                child_hash = hashChildrenPositional(
                    vec.data(), vec.size(), nodeHashes
                );
            } else {
                child_hash = hashChildrenSet(
                    vec.data(), vec.size(), nodeHashes
                );
            }
        }
        nodeHashes[node] = hashOperationParams(operations[node]) + child_hash;
    }
}

// Safely prints the constant value based on its type and sequence
void printConstant(const QueryGraph& graph, const Operation& op) {
    if (op.const_id == NODE_NONE) return;

    std::cout << " | Const: ";

    // Check if it's an array or a scalar
    bool is_array = (op.sequence == data::SequenceType::ARRAY); // Adjust enum value if needed

    if (!is_array) {
        // --- SCALARS ---
        if (op.type == data::ValueType::Int64 && op.const_id < graph.int64_constants.size()) {
            std::cout << graph.int64_constants[op.const_id];
        } else if (op.type == data::ValueType::Float64 && op.const_id < graph.float64_constants.size()) {
            std::cout << graph.float64_constants[op.const_id];
        } else if (op.type == data::ValueType::String && op.const_id < graph.string_constants.size()) {
            std::cout << "\"" << graph.string_constants[op.const_id] << "\"";
        } else {
            std::cout << "[ID: " << op.const_id << "]";
        }
    } else {
        // --- ARRAYS ---
        std::cout << "{ ";
        if (op.type == data::ValueType::Int64 && op.const_id < graph.int64_array_constants.size()) {
            const auto& vec = graph.int64_array_constants[op.const_id];
            for (size_t i = 0; i < vec.size(); ++i) std::cout << vec[i] << (i + 1 < vec.size() ? ", " : "");
        } else if (op.type == data::ValueType::Float64 && op.const_id < graph.float64_array_constants.size()) {
            const auto& vec = graph.float64_array_constants[op.const_id];
            for (size_t i = 0; i < vec.size(); ++i) std::cout << vec[i] << (i + 1 < vec.size() ? ", " : "");
        } else if (op.type == data::ValueType::String && op.const_id < graph.string_array_constants.size()) {
            const auto& vec = graph.string_array_constants[op.const_id];
            for (size_t i = 0; i < vec.size(); ++i) std::cout << "\"" << vec[i] << "\"" << (i + 1 < vec.size() ? ", " : "");
        }
        std::cout << " }";
    }
}

void printQueryGraph(const QueryGraph& graph) {
    std::cout << "\n=================================================================\n";
    std::cout << "                   QUERY GRAPH EXECUTION PLAN                    \n";
    std::cout << "=================================================================\n";

    // 1. Bucket the operations by their DataFrame ID
    std::map<node_t, std::vector<node_t>> df_groups;
    std::vector<node_t> standalone_nodes;

    for (node_t i = 0; i < graph.operations.size(); ++i) {
        node_t current_df = graph.operations[i].df;

        if (current_df == i) {
            // It is its own DataFrame (a Root Node)
            df_groups[i] = {};
        } else if (current_df != NODE_NONE) {
            // It is an operation belonging to a DataFrame
            df_groups[current_df].push_back(i);
        } else {
            // It has no DF attached
            standalone_nodes.push_back(i);
        }
    }

    // 2. Helper lambda to print a single formatted row
    auto printNode = [&](node_t id, bool is_child) {
        const Operation& op = graph.operations[id];

        // Indent children to show they run "inside" the DataFrame
        std::string prefix = is_child ? "    ├─ " : "▶ ";

        std::cout << prefix << "[" << std::setw(3) << id << "] "
                  << std::left << std::setw(22) << query::ColumnOperation_Name(op.op);

        std::cout << " | " << std::setw(8) << data::ValueType_Name(op.type);

        // Print Name (Fallback to DF name for reference if it has none)
        if (op.name != NODE_NONE && op.name < graph.names.size()) {
            std::cout << " | Name: '" << graph.names[op.name] << "'";
        } else if (is_child && op.df != NODE_NONE) {
            node_t parent_name_id = graph.operations[op.df].name;
            if (parent_name_id != NODE_NONE && parent_name_id < graph.names.size()) {
                std::cout << " | (DF: " << graph.names[parent_name_id] << ")";
            }
        }

        // Print Constants
        printConstant(graph, op);

        // Print Arguments (Edges to other nodes)
        if (id < graph.graph_mapping.size() && !graph.graph_mapping[id].empty()) {
            std::cout << " | Args: { ";
            for (size_t j = 0; j < graph.graph_mapping[id].size(); ++j) {
                std::cout << graph.graph_mapping[id][j] << (j + 1 < graph.graph_mapping[id].size() ? ", " : "");
            }
            std::cout << " }";
        }
        std::cout << "\n";
    };

    // 3. Print everything grouped perfectly
    for (const auto& [df_id, children] : df_groups) {
        std::cout << "-----------------------------------------------------------------\n";
        printNode(df_id, false); // Print the DF Root

        for (node_t child_id : children) {
            printNode(child_id, true); // Print all operations inside this DF
        }
    }

    if (!standalone_nodes.empty()) {
        std::cout << "-----------------------------------------------------------------\n";
        std::cout << "▶ STANDALONE / UNATTACHED NODES\n";
        for (node_t id : standalone_nodes) printNode(id, false);
    }

    std::cout << "=================================================================\n\n";
}
