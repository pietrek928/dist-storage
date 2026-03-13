grammar sql;

// =====================================================================
// PARSER RULES
// =====================================================================

// The entry point. WITH clauses can apply to SELECT, UPDATE, or DELETE
query: (WITH cte (',' cte)*)? statement EOF ;
cte: identifier AS '(' queryExpression ')' ;

// --- Statement Router ---
statement
    : queryExpression                                           # SelectStmt
    | updateStatement                                           # UpdateStmt
    | deleteStatement                                           # DeleteStmt
    ;

// --- 1. SELECT Statements ---
queryExpression: selectStatement (UNION ALL? selectStatement)* ;

selectStatement:
    SELECT selectElement (',' selectElement)*
    (FROM baseTable=relation joins+=joinClause*)?
    (WHERE whereExpr=expression)?
    (GROUP BY groupByExprs+=expression (',' groupByExprs+=expression)*)?
    (ORDER BY sortItems+=sortItem (',' sortItems+=sortItem)*)?
    (LIMIT limitExpr=expression)?
    (OFFSET offsetExpr=expression)?
    ;

selectElement
    : globalStar='*'                             // Global wildcard
    | tableName=identifier '.' tableStar='*'     // Table-specific wildcard
    | expression (AS? alias=identifier)?         // Standard expression / column
    ;
sortItem: expression dir=(ASC | DESC)? ;

relation: identifier (AS? alias=identifier)? | '(' queryExpression ')' (AS? alias=identifier)? ;
joinType: (INNER | LEFT | RIGHT | FULL) ;
joinClause: joinType? JOIN relation ON onExpr=expression ;

// --- 2. UPDATE Statements ---
updateStatement:
    UPDATE identifier SET assignmentList (WHERE expression)?
    ;

assignmentList: assignment (',' assignment)* ;
assignment: identifier '=' expression ;

// --- 3. DELETE Statements ---
deleteStatement:
    DELETE FROM identifier (WHERE expression)?
    ;

// --- Data Types ---
dataType: INT | FLOAT | BOOL | VARCHAR | STRING ;

// --- Expressions & Precedence ---
expression
    : '(' expression ')'                                        # ParenthesizedExpr
    | '(' queryExpression ')'                                   # SubqueryExpr
    | functionCall                                              # FunctionExpr
    | (tableName=identifier '.')? columnName=identifier         # ColumnExpr
    | literal                                                   # LiteralExpr
    | left=expression '::' dataType                             # CastExpr
    | op=(NOT | '~' | '!') expression                           # NotExpr
    | left=expression op=('*' | '/') right=expression           # MulDivExpr
    | left=expression op=('+' | '-') right=expression           # AddSubExpr
    | left=expression IS notOp=NOT? NULL_KW                     # IsNullExpr
    | left=expression notOp=NOT? IN '(' ( queryExpression | expression (',' expression)* ) ')' # InExpr
    | left=expression notOp=NOT? (LIKE | ILIKE) right=expression # LikeExpr
    | left=expression notOp=NOT? BETWEEN lower=expression AND upper=expression # BetweenExpr
    | left=expression (ops+=('=' | '==' | '<' | '>' | '<=' | '>=' | '!=' | '<>') rights+=expression)+ # ComparisonExpr
    | left=expression op=(AND | '&' | '&&') right=expression    # AndExpr
    | left=expression op=(XOR | '^') right=expression           # XorExpr
    | left=expression op=(OR | '|' | '||') right=expression     # OrExpr
    ;

// --- Functions ---
functionCall
    : (MAX | MIN | SUM | SQRT | ABS | LN) '(' expression ')'    # StandardFunction
    | COUNT '(' ('*' | expression) ')'                          # CountFunction
    ;

literal: STRING_LITERAL | INT_LITERAL | FLOAT_LITERAL | TRUE | FALSE | NULL_KW ;
identifier: IDENTIFIER ;


// =====================================================================
// LEXER RULES (Tokens)
// =====================================================================

// DML Commands
SELECT: [sS][eE][lL][eE][cC][tT] ;
UPDATE: [uU][pP][dD][aA][tT][eE] ;
SET:    [sS][eE][tT] ;
DELETE: [dD][eE][lL][eE][tT][eE] ;

// Core SQL
FROM:   [fF][rR][oO][mM] ;
WHERE:  [wW][hH][eE][rR][eE] ;
GROUP:  [gG][rR][oO][uU][pP] ;
BY:     [bB][yY] ;
ORDER:  [oO][rR][dD][eE][rR] ;
UNION:  [uU][nN][iI][oO][nN] ;
ALL:    [aA][lL][lL] ;
WITH:   [wW][iI][tT][hH] ;
AS:     [aA][sS] ;
ASC:    [aA][sS][cC];
DESC:   [dD][eE][sS][cC];


// Logical & Conditionals (UPDATED: Added BETWEEN and ILIKE)
AND:    [aA][nN][dD] ;
OR:     [oO][rR] ;
NOT:    [nN][oO][tT] ;
XOR:    [xX][oO][rR] ;
IN:     [iI][nN] ;
LIKE:   [lL][iI][kK][eE] ;
ILIKE:  [iI][lL][iI][kK][eE] ;
IS:     [iI][sS] ;
BETWEEN:[bB][eE][tT][wW][eE][eE][nN] ;
NULL_KW:[nN][uU][lL][lL] ;
TRUE:   [tT][rR][uU][eE] ;
FALSE:  [fF][aA][lL][sS][eE] ;

// Data Types
INT:    [iI][nN][tT] ;
FLOAT:  [fF][lL][oO][aA][tT] ;
BOOL:   [bB][oO][oO][lL] ;
VARCHAR:[vV][aA][rR][cC][hH][aA][rR] ;
STRING: [sS][tT][rR][iI][nN][gG] ;

// Joins
JOIN:   [jJ][oO][iI][nN] ;
INNER:  [iI][nN][nN][eE][rR] ;
LEFT:   [lL][eE][fF][tT] ;
RIGHT:  [rR][iI][gG][hH][tT] ;
FULL:   [fF][uU][lL][lL] ;
ON:     [oO][nN] ;

// Modifiers
LIMIT:  [lL][iI][mM][iI][tT] ;
OFFSET: [oO][fF][fF][sS][eE][tT] ;

// Functions
MAX:    [mM][aA][xX] ;
MIN:    [mM][iI][nN] ;
SUM:    [sS][uU][mM] ;
COUNT:  [cC][oO][uU][nN][tT] ;
SQRT:   [sS][qQ][rR][tT] ;
ABS:    [aA][bB][sS] ;
LN:     [lL][nN] ;

// Identifiers & Primitives
IDENTIFIER: [\p{L}_] [\p{L}\p{Nd}_]* ;
FLOAT_LITERAL: [0-9]+ '.' [0-9]+ ;
INT_LITERAL:   [0-9]+ ;
STRING_LITERAL: '\'' ( ~['] | '\'\'' )*? '\'' ;

SINGLE_LINE_COMMENT: ('--' | '//' | '#') ~[\r\n]* -> skip ;
MULTI_LINE_COMMENT: '/*' .*? '*/' -> skip ;
WS: [ \t\r\n]+ -> skip ;
