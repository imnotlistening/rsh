/*
 * Source a script. That is to say, run a script in this interactive shell.
 * This is how things like ~/.rshrc get parsed.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <rsh.h>
#include <exec.h>
#include <lexxer.h>
#include <builtin.h>

/* 
 * We need these to save our state and restore our state after the source
 * command finishes. */
extern char *script;
extern char **script_argv;
extern int interactive;
extern int EOF_encountered;

/*
 * Source a script, that is to say execute the script non-interactively in the
 * shell's environment. This is primarily useful for startup scripts: ~/.rshrc
 */
int builtin_source(int argc, char **argv, int in, int out, int err){

  int ret;
  int interactive_save;
  int EOF_encountered_save;
  char *script_save;
  char **script_argv_save;
  
  /* 
   * Save the shell's state (for when we have a source command in a sourced 
   * script or shell script) State include: args, numeric symbols in the symbol
   * table, etc.
   */
  

  /* Ignore 'source' in argv. */
  argv++;
  argc--;



  return ret;

}
