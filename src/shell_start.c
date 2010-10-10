
/**
 * Setup code for the shell. For now its pretty simple, we assume we are *not*
 * a login shell and just use stdin/stdout/stderr.
 */

/* Shell includes */
#include <rsh.h>
#include <exec.h>
#include <lexxer.h>
#include <symbol_table.h>

#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Global variables. Things like debug enable, etc. */
int debug = 0;
int login = 0;
int interactive = 0;

/* Used if we are executing a non-interactive script. These are filled
 * out by the arg parsing routines. */
char *script = NULL;
char **script_argv;

/* Function to source the init scripts. */
extern int builtin_source(int argc, char **argv, int in, int out, int err);

/* Options understood by the shell. */
static struct option options[] = {

  { "debug", 0, NULL, 'd' },
  { "login", 0, NULL, 'l' },
  { NULL, 0, NULL, 0 }

};

/**
 * This should be called by whatever main() is being used. Yay.
 */
int rsh_main(int argc, char **argv){
  
  int err, status;

  /* Parse args. */
  err = rsh_parse_args(argc, argv);
  if ( err ){
    fprintf(stderr, "Failed to parse args.\n");
    exit(1);
  }

  /* Set up terminal attributes, ignore some sigs, etc. */
  rsh_init();

  /* Handle calling the initialization scripts (~/.rshrc) */
  rsh_rc_init();

  /* If we are an interactive shell then go to interactive mode. */
  if ( interactive )
    status = interactive_shell();
  else
    status = script_shell();

  if ( interactive )
    printf("Good bye.\n");
  return 0;

}

int rsh_parse_args(int argc, char **argv){

  int c, args, i;

  /* First scan for options. */
  while ( (c = getopt_long(argc, argv, "dl", options, NULL)) != -1 ){

    switch (c){
    case 'd':
      debug = 1;
      break;
    case 'l':
      login = 1;
      break;
    case '?':
      return RSH_ERR;
    }

  }

  /* Anything left will first be interpretted as a script, then args to that
   * script. If nothing is left, then we are an interactive shell. 
   */
  if ( optind >= argc ){
    interactive = 1;
    return RSH_OK;
  }

  /* If we are here we have at least 1 remaining arg, possibly more. */
  script = argv[optind++];

  /* Now we need to fill out argv for the script. */
  args = argc - optind;
  script_argv = (char **)malloc(1 + (sizeof(char **) * args));
  for ( i = 0; i < args; i++){
    script_argv[i] = argv[optind++];
  }
  script_argv[i] = NULL;

  interactive = 0;

  return RSH_OK;

}

int interactive_shell(){

  printf("Running interactive RSH shell.\n");
  run_interactive();

  return 0;

}

int script_shell(){

  int script_file;

  /* Set up the lexxer. */
  script_file = open(script, O_RDONLY);
  if ( script_file < 0 ){
    perror(script);
    return RSH_ERR;
  }
  rsh_set_input(script_file);

  return run_script();

}

void rsh_init(){

  int err;

  /* Ignore these signals for interactive shells. */
  if ( interactive ){
    signal(SIGINT, rsh_history_display_async);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    //signal(SIGCHLD, SIG_IGN);
  }

  /* Set up the default environment, symbol table, etc. */
  err = symtable_init(50);
  if ( err ){
    fprintf(stderr, "Unable to init symbol table.\n");
    exit(1);
  }

  /* Save the real defaul (canonical) terminal mode. */
  save_default_term_settings();

  /* Initialize the terminal. */
  rsh_set_input(0);
  rsh_term_init();

  /* Initialize the RSH process group. */
  init_rsh_pgroup(5);

  /* Initialize the history buffer. */
  rsh_history_init();

  /* Initialize the default prompt. */
  prompt_init();

}

void rsh_rc_init(){

  /* See if $HOME/.rshrc exists. */
  int len, _argc;
  char *home;
  char *rc = "/.rshrc"; /* Make sure we have a path delimiter... */
  char *rc_script;
  char *_argv[3]; /* As in 'source' '$HOME/.rshrc' NULL */

  home = getenv("HOME");
  len = strlen(home) + strlen(rc) + 1;

  rc_script = (char *)malloc(len);
  if ( ! rc_script )
    return;

  strcpy(rc_script, home);
  strcat(rc_script, rc);
  printf("Loading: '%s'\n", rc_script);

  _argc = 2; /* As in 'source $HOME/.rshrc' */
  _argv[0] = "source";
  _argv[1] = rc_script;
  _argv[2] = NULL;
  builtin_source(_argc, _argv, 0, 1, 2);

}
