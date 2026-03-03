#include "parse_query.h"

#include <string>
#include <cctype> // For std::isxdigit
#include <iostream>

#include "../query/graph.h"
#include "query.pb.h"

// ANTLR Runtime header
#include <antlr4-runtime.h>

// Your generated headers
#include <sqlLexer.h>
#include <sqlParser.h>
#include <sqlBaseVisitor.h> // The base visitor class ANTLR generated for you

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

// ------------------------------------------------------------------
// 1. Define your custom Visitor
// ------------------------------------------------------------------
// Inherit from the generated BaseVisitor. You only need to override
// the functions for the specific grammar rules you care about.
class SqlVisitor : public sqlBaseVisitor {
    QueryGraph out_graph;
    antlr4::tree::ParseTreeProperty<node_t> nodeIds;

public:

    std::any visitSelectStatement(sqlParser::SelectStatementContext *ctx) override {
        // std::cout << "[VISITOR] Found a SELECT statement!\n";

        // visitChildren tells ANTLR to keep walking down the tree
        // to find the columns and table names.
        return visitChildren(ctx);
    }

    std::any visitLiteral(sqlParser::LiteralContext *ctx) override {
        std::any result = visitChildren(ctx);

        if (ctx->STRING_LITERAL()) {
            std::string str = ctx->STRING_LITERAL()->getText();
            str = unescapeSqlString(
                str.substr(1, str.size() - 2)
            );
            auto node_id = appendStringConstant(
                out_graph, str
            );
            nodeIds.put(ctx, node_id);
        } else if (ctx->INT_LITERAL()) {
            int64_t num = std::stoll(ctx->INT_LITERAL()->getText());
            auto node_id = appendInt64Constant(out_graph, num);
            nodeIds.put(ctx, node_id);
        } else if (ctx->FLOAT_LITERAL()) {
            double num = std::stod(ctx->FLOAT_LITERAL()->getText());
            auto node_id = appendFloat64Constant(out_graph, num);
            nodeIds.put(ctx, node_id);
        } else if (ctx->NULL_KW()) {
            auto node_id = appendNode(out_graph, 0, query::ColumnOperation::NULL_, data::ValueType::Any);
            nodeIds.put(ctx, node_id);
        } else if (ctx->TRUE()) {
            auto node_id = appendNode(out_graph, 0, query::ColumnOperation::TRUE, data::ValueType::Bool);
            nodeIds.put(ctx, node_id);
        } else if (ctx->FALSE()) {
            auto node_id = appendNode(out_graph, 0, query::ColumnOperation::FALSE, data::ValueType::Bool);
            nodeIds.put(ctx, node_id);
        }

        return result;
    }

    std::any visitMulDivExpr(sqlParser::MulDivExprContext *ctx) override {
        std::any result = visitChildren(ctx);

        query::ColumnOperation op;
        std::string operatorString = ctx->op->getText();
        if (operatorString == "*") {
            op = query::ColumnOperation::MUL;
        } else if (operatorString == "/") {
            op = query::ColumnOperation::DIV;
        } else {
            throw std::runtime_error("Unknown operator: " + operatorString);
        }

        auto left_id = nodeIds.get(ctx->left);
        auto right_id = nodeIds.get(ctx->right);
        auto node_id = appendNode(
            out_graph, NODE_NONE, op, data::ValueType::Any, {left_id, right_id}
        );
        nodeIds.put(ctx, node_id);

        return result;
    }

    std::any visitColumnList(sqlParser::ColumnListContext *ctx) override {
        // ctx->getText() grabs the exact string from the query that matched this rule
        // std::cout << "  -> Columns requested: " << ctx->getText() << "\n";
        return visitChildren(ctx);
    }

    std::any visitTableName(sqlParser::TableNameContext *ctx) override {
        // std::cout << "  -> Target table: " << ctx->getText() << "\n";
        return visitChildren(ctx);
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

    std::cout << "--------------------------------------\n\n";
}
