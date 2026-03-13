#include "parse_query.h"

#include <any>
#include <initializer_list>
#include <string>
#include <cctype> // For std::isxdigit
#include <iostream>

#include "../query/graph.h"
#include "data.pb.h"
#include "query.pb.h"

// ANTLR Runtime header
#include <antlr4-runtime.h>

// Your generated headers
#include <sqlLexer.h>
#include <sqlParser.h>
#include <sqlBaseVisitor.h> // The base visitor class ANTLR generated for you
#include <vector>

using namespace antlr4;


std::string unescapeSqlString(const std::string& rawInput) {
    std::string result;
    result.reserve(rawInput.length());

    for (size_t i = 0; i < rawInput.length(); ++i) {

        // 1. SQL-style escapes: '' becomes '
        if (rawInput[i] == '\'' && i + 1 < rawInput.length() && rawInput[i + 1] == '\'') {
            result += '\'';
            ++i;
        }

        // 2. Backslash escapes
        else if (rawInput[i] == '\\' && i + 1 < rawInput.length()) {
            char nextChar = rawInput[i + 1];

            // --- NEW: Hexadecimal Escapes (\xNN) ---
            if (nextChar == 'x' || nextChar == 'X') {
                // Check if we have enough characters and they are actually valid hex digits
                if (i + 3 < rawInput.length() &&
                    std::isxdigit(rawInput[i + 2]) &&
                    std::isxdigit(rawInput[i + 3]))
                {
                    // Extract the 2 hex characters and convert to a real char/byte
                    std::string hexStr = rawInput.substr(i + 2, 2);
                    char decodedChar = static_cast<char>(std::stoi(hexStr, nullptr, 16));

                    result += decodedChar;
                    i += 3; // Skip the 'x' and the two hex digits
                    continue; // Jump to the next iteration of the main loop
                }
            }

            // --- NEW: Octal Escapes (\000 to \377) ---
            // If the character following the backslash is a number from 0-7
            else if (nextChar >= '0' && nextChar <= '7') {
                int octalValue = 0;
                int count = 0;

                // Read up to 3 octal digits
                while (count < 3 && i + 1 + count < rawInput.length() &&
                       rawInput[i + 1 + count] >= '0' && rawInput[i + 1 + count] <= '7')
                {
                    octalValue = (octalValue * 8) + (rawInput[i + 1 + count] - '0');
                    count++;
                }

                result += static_cast<char>(octalValue);
                i += count; // Skip the octal digits
                continue;
            }

            // --- Standard C-style Escapes ---
            switch (nextChar) {
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                case '\\': result += '\\'; break;
                case '\'': result += '\''; break;
                case '\"': result += '\"'; break;
                case '0':  result += '\0'; break; // Null byte
                default:
                    // Unknown escape (e.g., \q), just keep the raw characters
                    result += '\\';
                    result += nextChar;
                    break;
            }
            ++i; // Skip the character we just processed
        }

        // 3. Normal characters
        else {
            result += rawInput[i];
        }
    }

    return result;
}

node_t findIdent(
    const std::string &ident,
    const std::initializer_list<const std::map<std::string, node_t>> &identMapStack
) {
    for (const auto &identMap : identMapStack) {
        auto ident_iter = identMap.find(ident);
        if (ident_iter != identMap.end()) {
            return ident_iter->second;
        }
    }
    return NODE_NONE;
}

// void pushContext(
//     std::vector<std::map<std::string, node_t>> &identMapStack
// ) {
//     identMapStack.emplace_back();
// }

// void popContext(
//     std::vector<std::map<std::string, node_t>> &identMapStack
// ) {
//     identMapStack.pop_back();
// }

class SqlVisitor : public sqlBaseVisitor {
    QueryGraph out_graph;
    std::map<std::string, node_t> fileTableContext;
    std::map<std::string, node_t> withTableContext;
    std::map<std::string, node_t> exprTableContext;
    std::map<std::string, node_t> exprVarContext;
    data::DBSchema db_schema;

    const std::map<std::string, query::ColumnOperation> opMap = {
        {"+", query::ColumnOperation::ADD},
        {"-", query::ColumnOperation::SUB},
        {"*", query::ColumnOperation::MUL},
        {"/", query::ColumnOperation::DIV},
        {"|", query::ColumnOperation::OR},
        {"OR", query::ColumnOperation::OR},
        {"&", query::ColumnOperation::AND},
        {"AND", query::ColumnOperation::AND},
        {"^", query::ColumnOperation::XOR},
        {"XOR", query::ColumnOperation::XOR},
        {"~", query::ColumnOperation::NOT},
        {"!", query::ColumnOperation::NOT},
        {"NOT", query::ColumnOperation::NOT},
        {"||", query::ColumnOperation::OR},
        {"&&", query::ColumnOperation::AND},
        {"!=", query::ColumnOperation::NEQ},
        {"<>", query::ColumnOperation::NEQ},
        {"<=", query::ColumnOperation::LTE},
        {">=", query::ColumnOperation::GTE},
        {"<", query::ColumnOperation::LT},
        {">", query::ColumnOperation::GT},
        {"=", query::ColumnOperation::EQ},
        {"==", query::ColumnOperation::EQ},
        {"IN", query::ColumnOperation::IN},
        {"NOT IN", query::ColumnOperation::NOT_IN},
        {"LIKE", query::ColumnOperation::LIKE},
        {"NOT LIKE", query::ColumnOperation::NOT_LIKE},
        {"ILIKE", query::ColumnOperation::ILIKE},
        {"NOT ILIKE", query::ColumnOperation::NOT_ILIKE},
        {"IS NULL", query::ColumnOperation::IS_NULL},
        {"IS NOT NULL", query::ColumnOperation::IS_NOT_NULL},
        {"NULL", query::ColumnOperation::NULL_},
        {"TRUE", query::ColumnOperation::TRUE},
        {"FALSE", query::ColumnOperation::FALSE},
    };

    node_t insertTableColumnNodes(const data::TableSchema &schema) {
        std::vector<node_t> col_node_ids;
        for (const auto &col : schema.columns()) {
            node_t col_node_id = appendColumn(out_graph, col.name(), col.type());
            col_node_ids.push_back(col_node_id);
        }
        node_t table_node_id = appendTable(out_graph, schema.name(), col_node_ids);
        return table_node_id;
    }

    node_t findOrInsertTable(const std::string &name) {
        node_t table_node_id = findIdent(name, {fileTableContext, withTableContext});
        if (table_node_id != NODE_NONE) {
            auto op = out_graph.operations[table_node_id].op;
            if (!isTableOperation(op)) {
                throw std::runtime_error("Invalid table name: " + name);
            }
            return table_node_id;
        }

        for (const auto &schema : db_schema.tables()) {
            if (schema.name() == name) {
                table_node_id = insertTableColumnNodes(schema);
                fileTableContext[name] = table_node_id;
                return table_node_id;  // Insert schema tables to top namespace
            }
        }

        throw std::runtime_error("Unknown table name: " + name);
    }

    node_t appendOperation(
        sqlParser::ExpressionContext *ctx,
        const std::string& opName,
        std::initializer_list<node_t> nodeArgs
    ) {
        const auto &op_iter = opMap.find(opName);
        if (op_iter == opMap.end()) {
            throw std::runtime_error("Unknown operator: " + opName);
        }
        query::ColumnOperation op_value = op_iter->second;

        return appendNode(
            out_graph, NODE_NONE, op_value, data::ValueType::Any, nodeArgs
        );
    }

    node_t visitNode(tree::ParseTree *tree) {
        return std::any_cast<node_t>(visit(tree));
    }

public:

    const QueryGraph &getGraph() const {
        return out_graph;
    }

    std::any visitLiteral(sqlParser::LiteralContext *ctx) override {
        visitChildren(ctx);

        if (ctx->STRING_LITERAL()) {
            std::string str = ctx->STRING_LITERAL()->getText();
            str = unescapeSqlString(
                str.substr(1, str.size() - 2)
            );
            return appendStringConstant(
                out_graph, str
            );
        } else if (ctx->INT_LITERAL()) {
            int64_t num = std::stoll(ctx->INT_LITERAL()->getText());
            return appendInt64Constant(out_graph, num);
        } else if (ctx->FLOAT_LITERAL()) {
            double num = std::stod(ctx->FLOAT_LITERAL()->getText());
            return appendFloat64Constant(out_graph, num);
        } else if (ctx->NULL_KW()) {
            return appendNode(out_graph, 0, query::ColumnOperation::NULL_, data::ValueType::Any);
        } else if (ctx->TRUE()) {
            return appendNode(out_graph, 0, query::ColumnOperation::TRUE, data::ValueType::Bool);
        } else if (ctx->FALSE()) {
            return appendNode(out_graph, 0, query::ColumnOperation::FALSE, data::ValueType::Bool);
        } else {
            throw std::runtime_error("Unknown literal: " + ctx->getText());
        }
    }

    std::any visitColumnExpr(sqlParser::ColumnExprContext *ctx) override {
        visitChildren(ctx);

        std::string var_name = ctx->columnName->IDENTIFIER()->getText();
        if (ctx->tableName) {
            var_name = ctx->tableName->getText() + "." + var_name;
        }

        node_t node_id = findIdent(var_name, {exprVarContext});
        if (node_id == NODE_NONE) {
            throw std::runtime_error("Unknown identifier: " + var_name);
        }
        return node_id;
    }

    std::any visitMulDivExpr(sqlParser::MulDivExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op->getText(),
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    std::any visitAddSubExpr(sqlParser::AddSubExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op->getText(),
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    std::any visitNotExpr(sqlParser::NotExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op->getText(),
            {visitNode(ctx->expression())}
        );
    }

    std::any visitLikeExpr(sqlParser::LikeExprContext *ctx) override {
        std::string opString = ctx->LIKE() ? "LIKE" : "ILIKE";
        if (ctx->notOp) {
            opString = "NOT " + opString;
        }

        return appendOperation(
            ctx,
            opString,
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    std::any visitBetweenExpr(sqlParser::BetweenExprContext *ctx) override {
        node_t lower_node_id = visitNode(ctx->lower);
        node_t middle_node_id = visitNode(ctx->left);
        node_t upper_node_id = visitNode(ctx->upper);

        node_t cmp1_id = appendNode(
            out_graph, NODE_NONE, query::ColumnOperation::LTE, data::ValueType::Any, {lower_node_id, middle_node_id}
        );
        node_t cmp2_id = appendNode(
            out_graph, NODE_NONE, query::ColumnOperation::LTE, data::ValueType::Any, {middle_node_id, upper_node_id}
        );

        return appendNode(
            out_graph, NODE_NONE,
            ctx->notOp ? query::ColumnOperation::NAND : query::ColumnOperation::AND,
            data::ValueType::Any, {cmp1_id, cmp2_id}
        );
    }

    std::any visitComparisonExpr(sqlParser::ComparisonExprContext *ctx) override {
        std::vector<node_t> cmp_node_ids;
        node_t left_node_id = visitNode(ctx->left);

        for (size_t i = 0; i < ctx->ops.size(); ++i) {
            node_t right_node_id = visitNode(ctx->rights[i]);

            std::string opString = ctx->ops[i]->getText();
            query::ColumnOperation opValue = opMap.at(opString);
            cmp_node_ids.push_back(
                appendNode(out_graph, NODE_NONE, opValue, data::ValueType::Any, {left_node_id, right_node_id})
            );

            left_node_id = right_node_id;
        }

        return appendNode(
            out_graph, NODE_NONE, query::ColumnOperation::AND,
            data::ValueType::Any, cmp_node_ids
        );
    }

    std::any visitAndExpr(sqlParser::AndExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op->getText(),
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    std::any visitXorExpr(sqlParser::XorExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op->getText(),
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    std::any visitOrExpr(sqlParser::OrExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op->getText(),
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    virtual std::any visitRelation(sqlParser::RelationContext *ctx) override {
        std::string alias;
        if (ctx->AS()) {
            alias = ctx->identifier().back()->getText();
        }

        node_t table_node_id;
        if (ctx->queryExpression()) {
            table_node_id = visitNode(ctx->queryExpression());
        } else {
            auto table_name = ctx->identifier(0)->getText();
            table_node_id = findOrInsertTable(table_name);
        }
        std::string expr_name = getNodeName(out_graph, table_node_id);
        if (expr_name.empty() and !alias.empty()) {
            setNodeName(out_graph, table_node_id, alias);
        }

        return table_node_id;
    }

    std::any visitSelectStatement(sqlParser::SelectStatementContext *ctx) override {
        std::map<std::string, node_t> exprTableContextOld;
        std::map<std::string, node_t> exprVarContextOld;
        exprTableContextOld.swap(exprTableContext);
        exprVarContextOld.swap(exprVarContext);

        try {
            std::vector<node_t> filters;

            // FROM
            std::vector<node_t> table_node_ids;
            if (ctx->baseTable) {
                node_t table_node_id = visitNode(ctx->baseTable);
                table_node_ids.push_back(table_node_id);
                std::string table_alias = (
                    ctx->baseTable->alias
                    ? ctx->baseTable->alias->getText()
                    : getNodeName(out_graph, table_node_id)
                );
                loadTableNames(exprVarContext, out_graph, table_node_id, table_alias);
                if (!table_alias.empty()) {
                    exprTableContext[table_alias] = table_node_id;
                }

                // JOIN
                for (auto joinCtx : ctx->joins) {
                    visit(joinCtx->relation());
                    node_t table_node_id = visitNode(joinCtx->relation());
                    table_node_ids.push_back(table_node_id);
                    std::string table_alias = (
                        joinCtx->relation()->alias
                        ? joinCtx->relation()->alias->getText()
                        : getNodeName(out_graph, table_node_id)
                    );
                    loadTableNames(exprVarContext, out_graph, table_node_id, table_alias);
                    if (!table_alias.empty()) {
                        exprTableContext[table_alias] = table_node_id;
                    }
                    // TODO: handle join type

                    // ON
                    if (joinCtx->onExpr) {
                        filters.push_back(
                            visitNode(joinCtx->onExpr)
                        );
                    }
                }
            }

            // SELECT items
            std::vector<node_t> column_node_ids;
            for (auto selectElement : ctx->selectElement()) {
                if (selectElement->globalStar) {
                    for (const auto &col : exprVarContext) {
                        if (col.first.find('.') == std::string::npos) {
                            column_node_ids.push_back(col.second);
                        }
                    }
                } else if (selectElement->tableStar) {
                    // TODO: put table columns here
                } else {
                    node_t element_node_id = visitNode(selectElement->expression());
                    if (selectElement->alias) {
                        setNodeName(out_graph, element_node_id, selectElement->alias->getText());
                    }
                    column_node_ids.push_back(element_node_id);
                }
            }

            // WHERE
            if (ctx->whereExpr) {
                filters.push_back(
                    visitNode(ctx->whereExpr)
                );
            }

            // GROUP BY
            std::vector<node_t> group_node_ids;
            for (auto groupExpr : ctx->groupByExprs) {
                group_node_ids.push_back(visitNode(groupExpr));
            }

            // ORDER BY
            std::vector<node_t> order_node_ids;
            for (auto sortItem : ctx->sortItems) {
                node_t item_node_id = visitNode(sortItem->expression());
                if (sortItem->DESC()) {
                    item_node_id = appendNode(
                        out_graph, NODE_NONE, query::ColumnOperation::DESC, data::ValueType::Any, {item_node_id}
                    );
                }
                order_node_ids.push_back(item_node_id);
            }

            // OFFSET
            node_t offset_node_id = NODE_NONE;
            if (ctx->offsetExpr) {
                offset_node_id = visitNode(ctx->offsetExpr);
            }

            // LIMIT
            node_t limit_node_id = NODE_NONE;
            if (ctx->limitExpr) {
                limit_node_id = visitNode(ctx->limitExpr);
            }

            // Construct graph nodes

            node_t df_node_id = appendNode(
                out_graph, NODE_NONE, query::ColumnOperation::DF, data::ValueType::Any, column_node_ids
            );

            // Tables will auto resolve from columns
            // df_node_id = appendNode(
            //     out_graph, NODE_NONE, query::ColumnOperation::JOIN, data::ValueType::Any, {df_node_id, table_node_ids}
            // );

            if (!filters.empty()) {
                node_t filter_node_id = NODE_NONE;
                if (filters.size() >= 2) {
                    filter_node_id = appendNode(
                        out_graph, NODE_NONE, query::ColumnOperation::AND, data::ValueType::Any, filters
                    );
                } else if (filters.size() == 1) {
                    filter_node_id = filters[0];
                }
                df_node_id = appendNode(
                    out_graph, NODE_NONE, query::ColumnOperation::FILTER,
                    data::ValueType::Any, {df_node_id, filter_node_id}
                );
            }

            if (!group_node_ids.empty()) {
                group_node_ids.insert(group_node_ids.begin(), df_node_id);
                df_node_id = appendNode(
                    out_graph, NODE_NONE, query::ColumnOperation::GROUP, data::ValueType::Any, group_node_ids
                );
            }

            if (!order_node_ids.empty()) {
                order_node_ids.insert(order_node_ids.begin(), df_node_id);
                df_node_id = appendNode(
                    out_graph, NODE_NONE, query::ColumnOperation::SORT, data::ValueType::Any, order_node_ids
                );
            }

            if (limit_node_id != NODE_NONE || offset_node_id != NODE_NONE) {
                if (offset_node_id == NODE_NONE) {
                    offset_node_id = appendInt64Constant(out_graph, 0);
                }
                if (limit_node_id != NODE_NONE) {
                    limit_node_id = appendNode(
                        out_graph, NODE_NONE, query::ColumnOperation::ADD,
                        data::ValueType::Int64, {offset_node_id, limit_node_id}
                    );
                }
                df_node_id = appendNode(
                    out_graph, NODE_NONE, query::ColumnOperation::RANGE,
                    data::ValueType::Any, {df_node_id, offset_node_id, limit_node_id}
                );
            }

            exprVarContext = std::move(exprTableContextOld);
            exprVarContext = std::move(exprVarContextOld);
            return df_node_id;
        } catch (const std::exception &e) {
            exprVarContext = std::move(exprTableContextOld);
            exprVarContext = std::move(exprVarContextOld);
            throw e;
        }
    }

    std::any visitQueryExpression(sqlParser::QueryExpressionContext *ctx) override {
        std::vector<node_t> select_node_ids;
        for (auto selectStatement : ctx->selectStatement()) {
            select_node_ids.push_back(visitNode(selectStatement));
        }

        if (select_node_ids.size() == 1) {
            return select_node_ids[0];
        } else {
            return appendNode(
                out_graph, NODE_NONE, query::ColumnOperation::UNION,
                data::ValueType::Any, select_node_ids
            );
        }
    }

    std::any visitUpdateStatement(sqlParser::UpdateStatementContext *ctx) override {
        throw std::runtime_error("Update unimplemented");
    }

    std::any visitDeleteStatement(sqlParser::DeleteStatementContext *ctx) override {
        throw std::runtime_error("Delete unimplemented");
    }

    std::any visitQuery(sqlParser::QueryContext *ctx) override {
        std::map<std::string, node_t> withTableContextOld;
        withTableContextOld.swap(withTableContext);

        try {
            for (auto cte : ctx->cte()) {
                withTableContext[cte->identifier()->getText()] = visitNode(cte);
            }

            node_t query_node_id = visitNode(ctx->statement());

            withTableContext = std::move(withTableContextOld);
            return query_node_id;
        } catch (const std::exception &e) {
            withTableContext = std::move(withTableContextOld);
            throw e;
        }
    }
};

// ------------------------------------------------------------------
// 2. The Parsing Function
// ------------------------------------------------------------------
void parseAndPrintSQL(const std::string& sqlString) {
    std::cout << "--- Parsing Query: " << sqlString << " ---\n";

    // A. Setup the input stream
    ANTLRInputStream input(sqlString);

    // B. Lexer reads characters and creates tokens
    sqlLexer lexer(&input);
    CommonTokenStream tokens(&lexer);

    // C. Parser reads tokens and builds the AST
    sqlParser parser(&tokens);

    // D. Start parsing at the 'query' rule (the top level of our grammar)
    tree::ParseTree *tree = parser.query();

    // (Optional) Print the raw LISP-style syntax tree generated by ANTLR
    // This is incredibly useful for debugging your grammar!
    std::cout << "Raw AST: " << tree->toStringTree(&parser) << "\n\n";

    // E. Traverse the tree using our custom Visitor
    SqlVisitor visitor;
    visitor.visit(tree);

    printQueryGraph(visitor.getGraph());
}

int main() {
    parseAndPrintSQL("SELECT 1");
    return 0;
}
