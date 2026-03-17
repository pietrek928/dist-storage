#include "parse_query.h"

#include <any>
#include <initializer_list>
#include <string>
#include <cctype> // For std::isxdigit
#include <iostream>

#include "../query/graph.h"
#include "ParserRuleContext.h"
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

std::string normalizeOp(std::string op) {
    op.erase(std::remove(op.begin(), op.end(), ' '), op.end());

    std::transform(op.begin(), op.end(), op.begin(),
        [](unsigned char c) { return std::toupper(c); }
    );

    return op;
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

class SqlCompileError : public std::runtime_error {
public:
    size_t line = 0;
    size_t column = 0;
    size_t length = 0;

    // 1. The Original Manual Constructor
    SqlCompileError(const std::string& message, size_t line, size_t column, size_t length = 1)
        : std::runtime_error(message), line(line), column(column), length(length) {}

    // 2. Convenience Constructor for Parser Rules (e.g., ctx, ctx->columnName)
    SqlCompileError(const std::string& message, antlr4::ParserRuleContext* ctx)
        : std::runtime_error(message) {
        if (ctx && ctx->getStart() && ctx->getStop()) {
            line = ctx->getStart()->getLine();
            column = ctx->getStart()->getCharPositionInLine();
            // Safely calculate the exact length of the entire rule text
            length = ctx->getStop()->getStopIndex() - ctx->getStart()->getStartIndex() + 1;
        }
    }

    // 3. Convenience Constructor for Lexer Tokens (e.g., ctx->globalStar)
    SqlCompileError(const std::string& message, antlr4::Token* token)
        : std::runtime_error(message) {
        if (token) {
            line = token->getLine();
            column = token->getCharPositionInLine();
            length = token->getStopIndex() - token->getStartIndex() + 1;
        }
    }
};

class ThrowingErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(
        antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol,
        size_t line, size_t charPositionInLine,
        const std::string &msg, std::exception_ptr e
    ) override {
        throw SqlCompileError("Compile error: " + msg, offendingSymbol);
    }
};

// Helper to extract a specific line from a multi-line string (1-indexed)
std::string getLineFromString(const std::string& text, size_t line_number) {
    std::istringstream stream(text);
    std::string line;
    for (size_t i = 0; i < line_number; ++i) {
        if (!std::getline(stream, line)) {
            return "<Line out of bounds>";
        }
    }
    return line;
}

// The main error printing function
void printSqlError(const std::string& raw_sql, const SqlCompileError& e) {
    std::cerr << "\n=========================================\n";
    std::cerr << "             SQL PARSE ERROR               \n";
    std::cerr << "=========================================\n";

    // 1. Print the actual error message
    std::cerr << "ERROR: " << e.what() << "\n\n";

    // 2. Extract the exact line where the error occurred
    std::string offending_line = getLineFromString(raw_sql, e.line);

    // 3. Setup the prefix so we can align our arrows perfectly
    std::string prefix = "LINE " + std::to_string(e.line) + ": ";

    // 4. Print the line of SQL code
    std::cerr << prefix << offending_line << "\n";

    // 5. Print the padding to align the underline
    // We pad with spaces equal to the prefix length + the column offset
    for (size_t i = 0; i < prefix.length() + e.column; ++i) {
        std::cerr << " ";
    }

    // 6. Print the exact underline highlighting the bad token
    for (size_t i = 0; i < e.length; ++i) {
        std::cerr << "^";
    }

    std::cerr << "\n=========================================\n\n";
}

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
        {"||", query::ColumnOperation::OR},
        {"OR", query::ColumnOperation::OR},
        {"&", query::ColumnOperation::AND},
        {"&&", query::ColumnOperation::AND},
        {"AND", query::ColumnOperation::AND},
        {"^", query::ColumnOperation::XOR},
        {"XOR", query::ColumnOperation::XOR},
        {"~", query::ColumnOperation::NOT},
        {"!", query::ColumnOperation::NOT},
        {"NOT", query::ColumnOperation::NOT},
        {"!=", query::ColumnOperation::NEQ},
        {"<>", query::ColumnOperation::NEQ},
        {"<=", query::ColumnOperation::LTE},
        {">=", query::ColumnOperation::GTE},
        {"<", query::ColumnOperation::LT},
        {">", query::ColumnOperation::GT},
        {"=", query::ColumnOperation::EQ},
        {"==", query::ColumnOperation::EQ},
        {"IN", query::ColumnOperation::IN},
        {"NOTIN", query::ColumnOperation::NOT_IN},
        {"LIKE", query::ColumnOperation::LIKE},
        {"NOTLIKE", query::ColumnOperation::NOT_LIKE},
        {"ILIKE", query::ColumnOperation::ILIKE},
        {"NOTILIKE", query::ColumnOperation::NOT_ILIKE},
        {"ISNULL", query::ColumnOperation::IS_NULL},
        {"ISNOTNULL", query::ColumnOperation::IS_NOT_NULL},
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

    node_t findOrInsertTable(ParserRuleContext *nameCtx) {
        std::string name = nameCtx->getText();

        node_t table_node_id = findIdent(
            name, {fileTableContext, withTableContext}
        );
        if (table_node_id != NODE_NONE) {
            auto op = out_graph.operations[table_node_id].op;
            if (!isTableOperation(op)) {
                throw SqlCompileError("Invalid table name: " + name, nameCtx);
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

        throw SqlCompileError("Unknown table name: " + name, nameCtx);
    }

    node_t appendOperation(
        ParserRuleContext *ctx,
        Token *opToken,
        const std::vector<node_t> &nodeArgs
    ) {
        const auto &op_iter = opMap.find(normalizeOp(opToken->getText()));
        if (op_iter == opMap.end()) {
            throw SqlCompileError("Unknown operator: " + opToken->getText(), opToken);
        }
        query::ColumnOperation op_value = op_iter->second;

        return appendNode(
            out_graph, NODE_NONE, op_value, data::ValueType::Any, nodeArgs
        );
    }

    node_t appendOperation(
        ParserRuleContext *ctx,
        ParserRuleContext *opCtx,
        const std::vector<node_t> &nodeArgs
    ) {
        const auto &op_iter = opMap.find(normalizeOp(opCtx->getText()));
        if (op_iter == opMap.end()) {
            throw SqlCompileError("Unknown operator: " + opCtx->getText(), opCtx);
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

    std::any visitLiteralExpr(sqlParser::LiteralExprContext *ctx) override {
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
            int64_t num = std::stoll(ctx->getText());
            return appendInt64Constant(out_graph, num);
        } else if (ctx->FLOAT_LITERAL()) {
            double num = std::stod(ctx->getText());
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

    std::any visitParenthesizedExpr(sqlParser::ParenthesizedExprContext *ctx) override {
        return visitNode(ctx->inside);
    }

    std::any visitColumnExpr(sqlParser::ColumnExprContext *ctx) override {
        visitChildren(ctx);

        std::string var_name = ctx->columnName->getText();
        if (ctx->tableName) {
            var_name = ctx->tableName->getText() + "." + var_name;
        }

        node_t node_id = findIdent(var_name, {exprVarContext});
        if (node_id == NODE_NONE) {
            throw SqlCompileError("Unknown identifier: " + var_name, ctx->columnName);
        }
        return node_id;
    }

    std::any visitMulDivExpr(sqlParser::MulDivExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op,
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    std::any visitAddSubExpr(sqlParser::AddSubExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op,
            {visitNode(ctx->left), visitNode(ctx->right)}
        );
    }

    std::any visitIsNullExpr(sqlParser::IsNullExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op, {}
        );
    }

    std::any visitNotExpr(sqlParser::NotExprContext *ctx) override {
        if (ctx->exprNot) {
            return appendOperation(
                ctx, ctx->op,
                {visitNode(ctx->exprNot)}
            );
        }
        return visitNode(ctx->exprPass);
    }

    std::any visitLikeExpr(sqlParser::LikeExprContext *ctx) override {
        return appendOperation(
            ctx, ctx->op,
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

        if (cmp_node_ids.size() >= 2) {
            return appendNode(
                out_graph, NODE_NONE, query::ColumnOperation::AND,
                data::ValueType::Any, cmp_node_ids
            );
        } else {
            return cmp_node_ids[0];
        }
    }

    std::any visitExpression(sqlParser::ExpressionContext *ctx) override {
        return visitNode(ctx->orExpr());
    }

    std::any visitAndExpr(sqlParser::AndExprContext *ctx) override {
        std::vector<node_t> and_node_ids = {visitNode(ctx->firstArg)};
        for (auto arg : ctx->args) {
            and_node_ids.push_back(visitNode(arg));
        }
        if (and_node_ids.size() == 1) {
            return and_node_ids[0];
        }
        return appendNode(
            out_graph, NODE_NONE, query::ColumnOperation::AND,
            data::ValueType::Any, and_node_ids
        );
    }

    std::any visitXorExpr(sqlParser::XorExprContext *ctx) override {
        std::vector<node_t> xor_node_ids = {visitNode(ctx->firstArg)};
        for (auto arg : ctx->args) {
            xor_node_ids.push_back(visitNode(arg));
        }
        if (xor_node_ids.size() == 1) {
            return xor_node_ids[0];
        }
        return appendNode(
            out_graph, NODE_NONE, query::ColumnOperation::XOR,
            data::ValueType::Any, xor_node_ids
        );
    }

    std::any visitOrExpr(sqlParser::OrExprContext *ctx) override {
        std::vector<node_t> or_node_ids = {visitNode(ctx->firstArg)};
        for (auto arg : ctx->args) {
            or_node_ids.push_back(visitNode(arg));
        }
        if (or_node_ids.size() == 1) {
            return or_node_ids[0];
        }
        return appendNode(
            out_graph, NODE_NONE, query::ColumnOperation::OR,
            data::ValueType::Any, or_node_ids
        );
    }

    virtual std::any visitRelation(sqlParser::RelationContext *ctx) override {
        std::string alias;
        if (ctx->alias) {
            alias = ctx->alias->getText();
        }

        node_t table_node_id;
        if (ctx->queryExpression()) {
            table_node_id = visitNode(ctx->queryExpression());
        } else {
            auto table_name = ctx->name;
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

            column_node_ids.insert(column_node_ids.begin(), table_node_ids.begin(), table_node_ids.end());
            node_t df_node_id = appendNode(
                out_graph, NODE_NONE, query::ColumnOperation::DF, data::ValueType::Any, column_node_ids
            );

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
            throw;
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
        throw SqlCompileError("Update unimplemented", ctx);
    }

    std::any visitDeleteStatement(sqlParser::DeleteStatementContext *ctx) override {
        throw SqlCompileError("Delete unimplemented", ctx);
    }

    std::any visitQuery(sqlParser::QueryContext *ctx) override {
        std::map<std::string, node_t> withTableContextOld;
        withTableContextOld.swap(withTableContext);

        try {
            for (auto cte : ctx->cte()) {
                withTableContext[cte->alias->getText()] = visitNode(cte->expr);
            }

            node_t query_node_id = visitNode(ctx->statement());

            withTableContext = std::move(withTableContextOld);
            return query_node_id;
        } catch (const std::exception &e) {
            withTableContext = std::move(withTableContextOld);
            throw;
        }
    }
};

// ------------------------------------------------------------------
// 2. The Parsing Function
// ------------------------------------------------------------------
void parseAndPrintSQL(const std::string& sqlString) {
    std::cout << "--- Parsing Query: " << sqlString << " ---\n";

    try {
        // A. Setup the input stream
        ANTLRInputStream input(sqlString);

        // B. Lexer reads characters and creates tokens
        sqlLexer lexer(&input);
        CommonTokenStream tokens(&lexer);

        // C. Parser reads tokens and builds the AST
        sqlParser parser(&tokens);

        ThrowingErrorListener errorListener;
        parser.removeErrorListeners(); // Remove default
        parser.addErrorListener(&errorListener);

        // D. Start parsing at the 'query' rule (the top level of our grammar)
        tree::ParseTree *tree = parser.query();

        // (Optional) Print the raw LISP-style syntax tree generated by ANTLR
        // This is incredibly useful for debugging your grammar!
        std::cout << "Raw AST: " << tree->toStringTree(&parser) << "\n\n";

        // E. Traverse the tree using our custom Visitor
        SqlVisitor visitor;
        visitor.visit(tree);

        auto graph = visitor.getGraph();
        fillDFIds(graph.operations, graph.graph_mapping);
        printQueryGraph(graph);
    } catch (const SqlCompileError& e) {
        // Catch our custom semantic/compilation errors
        printSqlError(sqlString, e);
    }
}

int main(int argc, const char *argv[]) {
    if (argc >= 2) {
        parseAndPrintSQL(argv[1]);
    }
    return 0;
}
