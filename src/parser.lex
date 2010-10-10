%{
#include <lexxer.h> /* Definitions for tokens */
#include <stdio.h>
extern char *cur_token;

/*
 * We define our own YY_INPUT routine so that the shell can do clever things
 * like not fail the assignment and do command line completion, history, etc.
 */
#define YY_INPUT(buf, result, max_size) result = rsh_readbuf(buf, max_size)

%}

WORD       [a-zA-Z0-9_\./\-!@%^()/\?~\+\[\]:,]+
SYMBOL     \$[a-zA-Z0-9_]+
SYMBOL2    \$\{[a-zA-Z0-9_]+\}

%s comment

%%
{WORD}           cur_token=yytext; return WORD;
{WORD}\=         cur_token=yytext; return SYMDEFINITION;
{SYMBOL}         cur_token=yytext; return SYMBOL;
{SYMBOL2}        cur_token=yytext; return SYMBOL;
\$               cur_token="$"; return WORD;
\$\*             cur_token="$*"; return SYMARGS;
`                return BACKTICK;
\'               return SINGLEQUOTE;
\"               return DOUBLEQUOTE;
\{               return OBRACE;
\}               return EBRACE;
\*               return GLOB;
<comment>;       cur_token=yytext; return WORD;
;                return TERMINATOR;
\n               BEGIN(INITIAL); return TERMINATOR;
\>\>             return APPEND_OUT;
2\>\>            return APPEND_ERR;
\<               return REDIRECT_IN;
\>               return REDIRECT_OUT;
2\>              return REDIRECT_ERR;
\|               return PIPE;
2\|              return PIPE_ERR;
\&               return BACKGROUND;
\#               BEGIN(comment); return COMMENT;
\\.              return ESCAPESEQ;
[ \t]+           cur_token=yytext; return WHITESPACE;
<<EOF>>          return EOFTOKEN;
%%

int yywrap(){
  return 1;
}
