
#ifndef _PARSER_H
# define _PARSER_H

extern int yylex(void);

/* What type of shell command state are we in? */
#define S_BASE    0
#define S_COMMAND 1
#define S_SYMDEF  2
#define S_REDIN   3
#define S_REDOUT  4
#define S_APPOUT  5

/* A token struct. Contains type and actual lexical data. */
struct rsh_token {

  char *tok;
  int   type;

};

/* Definitions for the parser's state. Mostly these are going to be used
 * for dealing with the preparse() function. */
#define STATE_NORM   0  /* Normal */
#define STATE_SQ     1  /* Single quote */
#define STATE_DQ     2  /* Double quote */
#define STATE_BT     3  /* Back tick; these are scary */
#define STATE_SYMDEF 6  /* Symbol defintion */

/* And of course, the parser functions. */
struct rsh_token *read_next_statement();
const char       *stringify_token(int type);
int               exec_token_seq(struct rsh_token *tokens);
char             *string_join(char *s1, char *s2);

/* These are the token storage functions. */
void              token_add(struct rsh_token **storage, 
			    int *size, struct rsh_token *tok);

/* And functions that you should not use. */
void              _append_to_tokseq(struct rsh_token *seq, int *size, 
				    struct rsh_token *tok);
int               _symbol_to_word_tok(struct rsh_token *sym, 
				      struct rsh_token *dest);
void              _print_tokseq(struct rsh_token *tokens);
void              _print_command(struct rsh_token *tokens);

#endif
