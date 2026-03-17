grammar sql;

// =====================================================================
// PARSER RULES
// =====================================================================

// The entry point. WITH clauses can apply to SELECT, UPDATE, or DELETE
query: (WITH cte (',' cte)*)? statement EOF ;
cte: alias=identifier AS '(' expr=queryExpression ')' ;

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

relation: name=identifier (AS? alias=identifier)? | '(' queryExpression ')' (AS? alias=identifier)? ;
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

// --- Expressions & Precedence ---
expression : orExpr ;
orExpr: firstArg=xorExpr ((OR | '|' | '||') args+=xorExpr)* ;
xorExpr: firstArg=andExpr ((XOR | '^') args+=andExpr)* ;
andExpr: firstArg=notExpr ((AND | '&' | '&&') args+=notExpr)* ;
notExpr: op=(NOT | '~' | '!') exprNot=notExpr | exprPass=cmpExpr;

cmpExpr
    : left=mathExpr (ops+=cmpOp rights+=mathExpr)+                         # ComparisonExpr
    | left=mathExpr op=isNullOp                                            # IsNullExpr
    | left=mathExpr op=inOp right=arrayExpression                          # InExpr
    | left=mathExpr op=likeOp right=mathExpr                               # LikeExpr
    | left=mathExpr notOp=NOT? BETWEEN lower=mathExpr AND upper=mathExpr   # BetweenExpr
    | mathExpr                                                             # MathFallthrough
    ;

// =====================================================================
// PART 2: ANTLR 4 STYLE (Flat Left-Recursive)
// Great for math! No loops needed, ANTLR builds the binary tree for you.
// =====================================================================
mathExpr
    : primaryExpr                                                          # PrimaryBase
    | left=mathExpr '::' dataType                                          # CastExpr
    | left=mathExpr op=('*' | '/') right=mathExpr                          # MulDivExpr
    | left=mathExpr op=('+' | '-') right=mathExpr                          # AddSubExpr
    ;

// =====================================================================
// 0. Base Elements
// =====================================================================
primaryExpr
    : '(' inside=expression ')'                                            # ParenthesizedExpr
    | '(' queryExpression ')'                                              # SubqueryExpr
    | functionCall                                                         # FunctionExpr
    | (tableName=identifier '.')? columnName=identifier                    # ColumnExpr
    | (STRING_LITERAL | '-'? INT_LITERAL | '-'? FLOAT_LITERAL | TRUE | FALSE | NULL_KW)   # LiteralExpr
    ;

cmpOp: '=' | '==' | '<' | '>' | '<=' | '>=' | '!=' | '<>' ;
isNullOp: IS NOT? NULL_KW ;
inOp: NOT? IN ;
likeOp: NOT? (LIKE | ILIKE) ;

arrayExpression: '(' ( queryExpression | expression (',' expression)* ) ')' ;

// --- Functions ---
functionCall
    : (MAX | MIN | SUM | SQRT | ABS | LN) '(' expression ')'    # StandardFunction
    | COUNT '(' ('*' | expression) ')'                          # CountFunction
    ;

dataType: INT | FLOAT | BOOL | VARCHAR | STRING ;
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
INT_LITERAL:   [0-9]+ ;
FLOAT_LITERAL
    : [0-9]+ '.' [0-9]* EXPONENT?   // Matches 1.2, 1., 1.2e3, 1.2E-5
    | '.' [0-9]+ EXPONENT?          // Matches .5, .5e-2
    | [0-9]+ EXPONENT               // Matches 1e3 (No dot, but has exponent)
    ;
fragment EXPONENT: [eE] [+-]? [0-9]+ ;
STRING_LITERAL: '\'' ( ~['] | '\'\'' )*? '\'' ;

SINGLE_LINE_COMMENT: ('--' | '//' | '#') ~[\r\n]* -> skip ;
MULTI_LINE_COMMENT: '/*' .*? '*/' -> skip ;
WS: [ \t\r\n]+ -> skip ;
