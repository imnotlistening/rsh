/**
 * Test out the rsh_exec() functionality.
 */

#include <exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>

void sig_handler(int sig);

extern char *script;
extern char **script_argv;
extern int interactive;

int main(){

  int err, status, state = 0;
  char *a_ptr = "exectest";
  char *prog = "/home/alex/tmp/sig";
  char **a_ptrptr = &a_ptr;
  struct rsh_process *tmp;

  script = NULL;
  script_argv = a_ptrptr;
  interactive = 1;

  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  //signal(SIGCHLD, SIG_IGN);

  err = init_rsh_pgroup(1);
  if ( err ){
    printf("Unable to make process group.\n");
    exit(1);
  }

  /* See how well rsh_exec() works for the shell. */
  printf("Execing: %s\n", prog);
  status = rsh_exec(prog, 1, &prog, 0, 1, 2, 0, 0, NULL);
  printf("returned status: %d\n", status);
  if ( WIFSTOPPED(status) ){
    printf("Process was stopped, starting in a second...\n");
    printf("  current pgrp: %ld\n", (long)tcgetpgrp(0));
    sleep(1);
    while ( (tmp = get_next_proc(&state)) != NULL ){
      /* Try and foreground the process. */
      if ( strcmp(tmp->name, prog) == 0 )
	status = foreground(tmp);
    }
    printf("returned status: %d\n", status);
  }
  return 0;

}


void sig_handler(int sig){

  printf("SHELL: sig=%d, handled.\n", sig);

}
