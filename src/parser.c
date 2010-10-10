/**
 * Grammar parser for the shell. Yikes. I wish I could have used yacc here but
 * oh well. Yacc didn't seem to have any way to do interactive stuff.
 */

#include <rsh.h>
#include <lexxer.h>
#include <parser.h>
#include <symbol_table.h>

#include <string.h>
#include <stdlib.h>

/* Table of string names of token types. The indices of this table correspond
 * to the values of the lexxer's defines. */
char *lexxer_tokens[] = {

  "NULL_LEX",
  "WORD",
  "SYMDEFINTION",
  "SYMBOL",
  "TERMINATOR",
  "BACKTICK",
  "SINGLEQUOTE",
  "DOUBLEQUOTE",
  "OBRACE",
  "EBRACE",
  "GLOB",
  "REDIRECT_IN",
  "REDIRECT_OUT",
  "REDIRECT_ERR",
  "APPEND_OUT",
  "APPEND_ERR",
  "PIPE",
  "WHITESPACE",
  "COMMENT",
  "EOFTOKEN",
  "BACKGROUND",
  "ESCAPESEQ",
  "SYMARGS",

};

/* This is the current token, it is used to get input from the lexxer. */
char *cur_token;

/* Temporary token storage pointer. */
struct rsh_token *tok_storage;

/* This is useful. */
struct rsh_token null_tok = { NULL, NULL_LEX };

int EOF_encountered = 0;  /* Save this. */

/**
 * Read a statement from the lexxer and return it as a sequence of tokens.
 */
struct rsh_token *read_next_statement(){

  struct rsh_token *tokens = NULL;
  int toktype;
  int size = 0;   /* This will grow as we get more tokens. */

  if ( EOF_encountered ){
    //printf("<EOF>\n");
    return NULL;
  }

  cur_token = NULL;

  /* Init the token storage. */
  token_add(&tokens, &size, &null_tok);

  while ( 1 ){
    
    cur_token = NULL;
    toktype = yylex();

    if ( toktype == TERMINATOR )
      break;

    if ( toktype == EOFTOKEN ){
      EOF_encountered = 1;
      break;
    }

    struct rsh_token tok = { cur_token, toktype };
    //printf("{%s:%s}", stringify_token(toktype), cur_token);
    token_add(&tokens, &size, &tok);

  }

  return tokens;

}

inline const char *stringify_token(int type){

  return lexxer_tokens[type];

}

int token_seqlen(struct rsh_token *tokens){

  int len = 0;
  while ( tokens[len].type != NULL_LEX )
    len++;

  return len;

}

/*
 * preparse() will take a sequence of tokens and condense them into several 
 * token types:
 *   WORD
 *   SYMDEFINITION
 *   REDIRECT_IN
 *   REDIRECT_OUT
 *   APPEND_OUT
 *   PIPE
 * 
 * The goal is to handle all interpolation (variable substitution and backtick
 * escaped commands) so that the rest of the parser only has to deal with 
 * words, symbol definitions/assignments, redirections, pipes, and background 
 * symbol things. Hopefully this will be easier than handling things like 
 * quoted strings else where.
 */
struct rsh_token *preparse(struct rsh_token *tokens){

  struct rsh_token prev_tok = {NULL, NULL_LEX};
  struct rsh_token tmp;
  struct rsh_token numeric_var;
  struct rsh_token *new_seq = NULL;
  struct sym_entry *status;
  char *name, *data;
  int seq_len = token_seqlen(tokens);
  int tokind = 0;
  int size = 0;
  int state = STATE_NORM;
  int backticks = 0;
  int start;

  /* Init the new sequence of tokens. */
  token_add(&new_seq, &size, &null_tok);
  
  while ( tokind < seq_len){

    switch (tokens[tokind].type){

      /*
       * Handle a WORD token. Either append it to the previous word in the
       * case of quotes, or just straight up add it to the seq list.
       */
    case WORD:
      if ( prev_tok.type == WORD ){
	_append_to_tokseq(new_seq, &size, &(tokens[tokind]));
      } else {
	token_add(&new_seq, &size, &(tokens[tokind]));
      }
      break;

      /*
       * Sometimes a token that looks like a symbol definition really is
       * something else, for instance: blah=blah=blah or maybe blah="blah="
       */
    case SYMDEFINITION:
      /* Sometimes a symbol defintion is only a WORD. */
      if ( state == STATE_SQ || state == STATE_DQ || state == STATE_SYMDEF){
	_append_to_tokseq(new_seq, &size, &(tokens[tokind]));
	break;
      }

      /* OK, this actually is a symbol definition. */
      token_add(&new_seq, &size, &(tokens[tokind]));
      state = STATE_SYMDEF;
      break;

      /*
       * Interpolate a symbol.
       */
    case SYMBOL:
      tmp.tok = NULL; /* Clear tmp. */
      tmp.type = NULL_LEX;
      _symbol_to_word_tok(&(tokens[tokind]), &tmp);
      _append_to_tokseq(new_seq, &size, &tmp);
      break;

      /*
       * Shudder. Just Shudder.
       */
    case BACKTICK:
      if ( backticks )
	backticks = 0;
        /* Exec the command enclosed and interpolate its output. */
      else
	backticks = STATE_BT;
      break;

      /*
       * Quote out some text including shell meta characters.
       */
    case SINGLEQUOTE:
      if ( state == STATE_DQ ){
	struct rsh_token tmp = {"'", WORD};
	_append_to_tokseq(new_seq, &size, &tmp);
      }	else if ( state == STATE_SQ ) {
	state = STATE_NORM;
      } else {
	state = STATE_SQ;
      }
      break;

      /*
       * Quote out some text but leave in meta characters.. (i.e do 
       * interpolation).
       */
    case DOUBLEQUOTE:
      if ( state == STATE_SQ ){
	struct rsh_token tmp = {"\"", WORD};
	_append_to_tokseq(new_seq, &size, &tmp);
      }	else if ( state == STATE_DQ ) {
	state = STATE_NORM;
      } else {
	state = STATE_DQ;
      }
      break;
      
      /*
       * Ignored for now. Will be useful if I want to try and implement
       * functions.
       */
    case OBRACE:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	tmp.type = WORD;
	tmp.tok  = "{";
	_append_to_tokseq(new_seq, &size, &tmp);
      } else {
	tmp.type = WORD;
	tmp.tok  = "{";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Ignored for now (see comment for case OBRACE above).
       */
    case EBRACE:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	tmp.type = WORD;
	tmp.tok  = "}";
	_append_to_tokseq(new_seq, &size, &tmp);
      } else {
	tmp.type = WORD;
	tmp.tok  = "}";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;
      
      /*
       * Yikes, this one is scarey. For now don't bother with it, pass it as
       * is. I will try and implement globbing later...
       */
    case GLOB:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	tmp.type = WORD;
	tmp.tok  = "*";
	_append_to_tokseq(new_seq, &size, &tmp);
      } else {
	tmp.type = WORD;
	tmp.tok  = "*";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * A redirection. Just pass this along unless we are being quoted.
       */
    case REDIRECT_IN:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	tmp.type = WORD;
	tmp.tok  = "<";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Much like REDIRECT_IN.
       */
    case REDIRECT_OUT:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	tmp.type = WORD;
	tmp.tok  = ">";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Much like REDIRECT_IN.
       */
    case REDIRECT_ERR:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	tmp.type = WORD;
	tmp.tok  = "2>";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Much like REDIRECT_IN.
       */
    case APPEND_OUT:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	tmp.type = WORD;
	tmp.tok  = ">>";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Much like REDIRECT_IN.
       */
    case APPEND_ERR:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	tmp.type = WORD;
	tmp.tok  = "2>>";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Much like REDIRECT_IN.
       */
    case PIPE:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	tmp.type = WORD;
	tmp.tok  = "|";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Much like REDIRECT_IN.
       */
    case BACKGROUND:
      /* Not in quotes. */
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	tmp.type = WORD;
	tmp.tok  = "&";
	_append_to_tokseq(new_seq, &size, &tmp);
      }
      break;

      /*
       * Ugh, someone specified $*. Why did they have to do that... Anyway this
       * gets expanded to $1 $2 $3 ... $n where n is the largest numeric symbol
       * in the symbol table. These are meant to correspond to the arguments
       * passed to a script; for example: 
       *
       *  ./rsh script.sh hello world
       *
       * In this case $1 is 'hello', $2 is 'world'. There is also another
       * numeric symbol $0 which corresponds to the script name but that does
       * not get exapanded by $*.
       */
    case SYMARGS:

      /*
       * This is really easy.
       */
      if ( state == STATE_SQ ){
	tmp.tok = "$*";
	tmp.type = WORD;
	_append_to_tokseq(new_seq, &size, &tmp);
	break;
      }

      /* Ignore the $0 variable. */
      status = NULL;
      name = NULL;
      data = NULL;
      symtable_numeric(&status, &name, &data);

      tmp.tok = " ";
      tmp.type = WHITESPACE;

      start = 1; /* We are starting. */

      while ( symtable_numeric(&status, &name, &data) ){

	numeric_var.type = WORD;
	numeric_var.tok = data;
	
	/*
	 * Handle inside double quotes...
	 */
	if ( prev_tok.type == WORD ){

	  /*
	   * This handles the white space that precedes a numeric variable. In
	   * the case that this is the first ($1 for instance) then we should
	   * not prepend a white space. Thus "$*" exapnds to "$1 $2 ... $n"
	   * instead of " $1 $2 ... $n".
	   */
	  if ( ! start )
	    _append_to_tokseq(new_seq, &size, &tmp);
	  else
	    start = 0;

	  /*
	   * Append the value of the numeric itself.
	   */
	  _append_to_tokseq(new_seq, &size, &numeric_var);

	} else {

	  /*
	   * Same idea as above; we don't need a whitespace if this is the
	   * first of the sequence of numeric variables.
	   */
	  if ( ! start )
	    token_add(&new_seq, &size, &tmp);
	  else
	    start = 0;

	  /*
	   * Just add the symbol 
	   */
	  token_add(&new_seq, &size, &numeric_var);

	}
	
      }

      break;

      /*
       * White space is our deliniator. However, if we are being quoted, turn 
       * the white space into a WORD.
       */
    case WHITESPACE:
      if ( state != STATE_SQ && state != STATE_DQ ){
	token_add(&new_seq, &size, &(tokens[tokind]));
      } else {
	_append_to_tokseq(new_seq, &size, &(tokens[tokind]));
      }
      break;

      /*
       * We are done with this line. Although if we are quoted pass it along
       * as a word.
       */
    case COMMENT:
      /* We are done if we find an unquoted comment symbol. */
      if ( state != STATE_SQ && state != STATE_DQ )
	return new_seq;

      /* Otherwise, we just add the comment symbol to the previous word. */
      break;
    default:
      break;
    }

    prev_tok.type = new_seq[token_seqlen(new_seq)-1].type;
    prev_tok.tok  = new_seq[token_seqlen(new_seq)-1].tok;
    tokind++;

  }

  return new_seq;

}

int exec_token_seq(struct rsh_token *tokens){

  struct rsh_token *real_tokens;

  /* Preparse the string for interpolation, etc. */
  real_tokens = preparse(tokens);
  if ( ! token_seqlen(real_tokens) )
    return 0;
  //_print_command(real_tokens);

  /* Exec this command sequence. */
  return rsh_command(real_tokens);

}

/* Token storage functions. These are for convienence. */
void token_add(struct rsh_token **storage, int *size, struct rsh_token *tok){

  struct rsh_token *tmp;
  struct rsh_token *storage_elems;
  int offset;

  /* Initialize. */
  if ( ! (*storage) ){
    (*storage) = (struct rsh_token *)malloc(8 * sizeof(struct rsh_token));
    *size = 8;
    memset(*storage, 0, 8 * sizeof(struct rsh_token));
  }

  /* See if we have enough space, if not, add some. */
  offset = token_seqlen(*storage);
  if ( (offset + 1) >= *size){
    tmp = malloc(sizeof(struct rsh_token) * ((*size) + 8));
    memcpy(tmp, *storage, (*size) * sizeof(struct rsh_token));
    free(*storage);
    *size += 8;
    *storage = tmp;
  }

  /* Add the element at the end and make sure we null terminate. */
  //printf("    Adding elem (%d): %s -> %s\n", 
  //	 offset, stringify_token(tok->type), tok->tok);
  storage_elems = *storage;
  storage_elems += offset;

  storage_elems->type = tok->type;
  storage_elems->tok  = tok->tok ? strdup(tok->tok) : NULL;

  storage_elems++;
  storage_elems->type = NULL_LEX;
  storage_elems->tok  = NULL;

}

/**
 * A useful string manipulation function.
 */
char *string_join(char *s1, char *s2){

  char *new_str;
  int new_len;

  /* If 1 of the passed strings is NULL, return a dupe of the other. */
  if ( s1 && ! s2 ){
    return strdup(s1);
  }
  if ( ! s1 && s2 ){
    return strdup(s2);
  }

  new_len = strlen(s1) + strlen(s2);
  new_str = (char *)malloc(new_len + 1);
  memcpy(new_str, s1, strlen(s1));
  memcpy(new_str+strlen(s1), s2, strlen(s2));
  new_str[new_len] = 0;

  return new_str;

}

/* Highly specialized internal functions. Don't use unless you *really* know
 * what you are doing. */

/**
 * Append a token onto the previous WORD token. If the previous token is
 * not a WORD, then just add the passed token. The passed token is 
 * automagically turned into a WORD.
 */
void _append_to_tokseq(struct rsh_token *seq, int *size, 
			    struct rsh_token *tok){

  struct rsh_token *prev_tok;
  struct rsh_token add_tok;
  char *tmp;
  int len;

  //printf("  Appending to tok seq\n");
  /* First get the previous token and make our add token. */
  len = token_seqlen(seq);
  add_tok.tok = tok->tok;
  add_tok.type = WORD;

  /* If the len is 0, just add the word (it's the first) and return. */
  if ( len <= 0 ){
    token_add(&seq, size, &add_tok);
    return;
  }

  prev_tok = seq + (len-1);
  //printf("  prev_tok address: %p\n", prev_tok);

  /* Now that we have prev_tok, see what we should do. */
  if ( prev_tok->type != WORD ){
    //printf("  Prev != WORD... just adding\n");
    token_add(&seq, size, &add_tok);
  } else {
    if ( prev_tok->tok ){
      tmp = string_join(prev_tok->tok, tok->tok);
      free(prev_tok->tok);
      //printf("  tok->tok: %p\n", tok->tok);
      //printf("  tmp:%p -> %s\n", tmp, tmp);
      prev_tok->tok = tmp;
    } else {
      prev_tok->tok = strdup(tok->tok);
    }
  }

  return;

}

/**
 * Convert a symbol token to the data it represents and put that new word
 * token into the token pointed to by dest. 
 */
int _symbol_to_word_tok(struct rsh_token *sym, struct rsh_token *dest){

  char *data;
  char *tmp_sym, *working_sym;

  if ( sym->type != SYMBOL )
    return RSH_ERR;

  tmp_sym = strdup(sym->tok);
  working_sym = tmp_sym+1; /* Ignore the preceding '$' sign */
 
  if ( working_sym[0] == '{' ){
    working_sym++;
    working_sym[strlen(working_sym)-1] = 0; /* Wipe out the trailing '{' */
  }

  //printf("  Resolving symbol: %s ", working_sym);

  data = symtable_get(working_sym);
  free(tmp_sym);
  
  dest->tok = data ? strdup(data) : NULL;
  dest->type = WORD;

  //printf("(done)\n");
  return RSH_OK;

}

/**
 * Internal debugging function for displaying a sequence of tokens.
 */
void _print_tokseq(struct rsh_token *tokens){

  int ind = 0;
  while ( tokens[ind].type != NULL_LEX ){
    printf("{%d:%s}", tokens[ind].type, tokens[ind].tok);
    ind++;
  }
  printf("\n");

}

/**
 * Internal debugging function for displaying a command that is represented
 * by a sequence of tokens.
 */
void _print_command(struct rsh_token *tokens){

  int ind = 0;
  while ( tokens[ind].type != NULL_LEX ){
    switch (tokens[ind].type){
    case WORD:
      printf("'%s' ", tokens[ind].tok);
      break;
    case SYMDEFINITION:
      printf("'%s' ", tokens[ind].tok);
      break;
    case SYMBOL:
    case TERMINATOR:
    case BACKTICK:
    case SINGLEQUOTE:
    case DOUBLEQUOTE:
    case OBRACE:
    case EBRACE:
      break; /* These wont be in preprocessed commands. */
    case GLOB:
      printf("'*' ");
      break;
    case REDIRECT_IN:
      printf("'<' ");
      break;
    case REDIRECT_OUT:
      printf("'>' ");
      break;
    case APPEND_OUT:
      printf("'>>' ");
      break;
    case PIPE:
      printf("'|' ");
      break;
    case WHITESPACE:
      printf("' ' ");
      break;
    case COMMENT:
    case EOFTOKEN:
      break; /* Nor will these. */
    case BACKGROUND:
      printf("'&' ");
      break;
    case ESCAPESEQ:
      break;
    }
    ind++;
  }
  printf("\n");

}

