
/**
 * Setup code for the shell. For now its pretty simple, we assume we are *not*
 * a login shell and just use stdin/stdout/stderr.
 */

/* Shell includes */
#include <rsh.h>
#include <exec.h>
#include <rshfs.h>
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

char *shell_name;

/* Name of the built in file system image. */
char *bifs = "builtin_fs.img";

/* Stuff specific to the FAT16 used. */
long int geometry[2] = { 5*1024*1024, 8*1024 };
int override = 0; /* If set, override the limits imposed. */
extern int _rsh_fat16_geometry(char *geometry, long int *geo);

/* Function to source the init scripts. */
extern int builtin_source(int argc, char **argv, int in, int out, int err);

/* SIGCHLD handler. This guy is pretty important. */
void sigchld_handler(int sig);

/* Options understood by the shell. */
static struct option options[] = {

  { "debug", 0, NULL, 'd' },
  { "login", 0, NULL, 'l' },
  { "filesystem", 1, NULL, 'f' }, 
  { "geometry", 1, NULL, 'g' },
  { "native", 1, NULL, 'n' },
  { "override", 0, NULL, 'o' },
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

  shell_name = argv[0];

  /* First scan for options. */
  while ( (c = getopt_long(argc, argv, "dl", options, NULL)) != -1 ){

    switch (c){
    case 'd':
      debug = 1;
      break;
    case 'l':
      login = 1;
      break;
    case 'f':
      bifs = optarg;
      break;
    case 'g':
      if ( _rsh_fat16_geometry(optarg, geometry) ){
	fprintf(stderr, "Warning: unable to parse geometry.\n");
	/* Go back to default I guess. */
	geometry[0] = 5*1024*1024;
	geometry[1] = 8*1024;
      }
      break;
    case 'o':
      override = 1;
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
    signal(SIGCHLD, sigchld_handler);
  }

  /* Set up the default environment, symbol table, etc. */
  err = symtable_init(50);
  if ( err ){
    fprintf(stderr, "Unable to init symbol table.\n");
    exit(1);
  }

  /* Save the real default (canonical) terminal mode. */
  save_default_term_settings();

  /* Init the internal file system. */
  rsh_init_fs();

  if ( geometry[0] < (5 * 1024*1024) || geometry[0] > (50 * 1024*1024) ){
    if ( ! override ){
      printf("Disk size must be between 5 and 50 Megabytes."
	     " %d not acceptable\n", geometry[0]);
      printf(
	"Specify --override to force a disk size outside of these limits.\n");
      exit(1);
    } else {
      printf("Overriding default limits.\n");
    }
  }
  if ( geometry[1] < (8*1024) || geometry[1] > (16*1024) ){
    if ( ! override ){
      printf("Cluster size must be between 8 and 16 KBytes.\n");
      printf(
	"Specify --override to force a disk size outside of these limits.\n");
      exit(1);
    } else {
      printf("Overriding default limits.\n");
    }
  }
  if ( geometry[1] & 0x3ff ){
    printf("Cluster size not a multiple of 1KByte; this is bad: you cannot\n");
    printf("override this.\n");
    exit(1);
  }

  printf("Loading disk image: %s (%ld:%ld)\n", bifs, geometry[0], geometry[1]);
  err = rsh_fat16_init(bifs, geometry[0], geometry[1]);
  if ( err ){
    printf("WARNING: Could not load internal FS.\n");
  }

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
  struct stat sbuf;

  home = getenv("HOME");
  len = strlen(home) + strlen(rc) + 1;

  rc_script = (char *)malloc(len);
  if ( ! rc_script )
    return;

  strcpy(rc_script, home);
  strcat(rc_script, rc);

  /* See if the file exists and is accessable, etc, etc. If not fail 
   * silently. */
  if ( stat(rc_script, &sbuf) < 0)
    return;
  
  _argc = 2; /* As in 'source $HOME/.rshrc' */
  _argv[0] = "source";
  _argv[1] = rc_script;
  _argv[2] = NULL;
  builtin_source(_argc, _argv, 0, 1, 2);

}

void rsh_exit(int status){

  fflush(stdout);
  fflush(stderr);

  /* If I cared, maybe clean up child processes or something. But whatever
   * I don't have time anymore :(. */
  exit(status);

}
