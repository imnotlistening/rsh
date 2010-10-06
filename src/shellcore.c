/**
 * Handles the execution of a script.
 */

#include <rsh.h>
#include <exec.h>
#include <lexxer.h>
#include <parser.h>
#include <symbol_table.h>

#include <stdio.h>
#include <stdlib.h>

/* External script variables. */
extern char *script;
extern char **script_argv;

extern int yylex();

int run_script(){

  struct rsh_token *tokens;
  char buf[6]; /* For the snprintf. */
  char **argv = script_argv;
  int argc = 1;

  /* Command name is $0 */
  symtable_add("0", script);

  /* We need to make sure argv shows up in the symbol table as $1, $2, etc */
  while ( *argv != NULL){
    snprintf(buf, 6, "%d", argc);
    symtable_add(buf, *argv);
    argv++;
    argc++;
  }

  while ( ( tokens = read_next_statement()) != NULL ){
    
    /* Attempt to execute the string of tokens we were passed. */
    exec_token_seq(tokens);
    free(tokens);

  }

  return RSH_OK;

}

int run_interactive(){

  struct rsh_token *tokens;

  /* Display the prompt, get input, run command, etc. */  
  do {

    /* Get the command to execute. */
    prompt_print();
    tokens = read_next_statement();
    if ( ! tokens )
      continue;

    /* Run the command. */
    exec_token_seq(tokens);

    /* Do process book keeping. */
    check_processes();

  } while (1);

  return 0;

}
