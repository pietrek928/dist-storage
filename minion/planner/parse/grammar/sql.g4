grammar sql;

// --- PARSER RULES (The Grammar) ---
// The root rule. Every query must end with an End Of File (EOF).
query: selectStatement EOF ;

selectStatement: SELECT columnList FROM tableName ;

// A column list is either a single identifier, multiple comma-separated identifiers, or a star.
columnList: IDENTIFIER (',' IDENTIFIER)* | '*' ;

tableName: IDENTIFIER ;

// --- LEXER RULES (The Tokens) ---
SELECT: [sS][eE][lL][eE][cC][tT] ;  // Case-insensitive 'SELECT'
FROM: [fF][rR][oO][mM] ;            // Case-insensitive 'FROM'

// An identifier starts with a letter/underscore, followed by letters/numbers/underscores
IDENTIFIER: [a-zA-Z_][a-zA-Z0-9_]* ;

// Ignore spaces, tabs, and newlines entirely
WS: [ \t\r\n]+ -> skip ;
