/*
 * Execute a command with the passed environment. The environment consists of a
 * list of file descriptors, argv, argc, the program name itself, and a env
 * taken from the parent process env.
 *
 * Once the process has been succesfully executed in a new child process, the
 * child process is added to the list of currently executing child processes.
 * That list can then be used for job control.
 */

#include <rsh.h>
#include <exec.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/types.h>

/* 
 * I *hate* solaris.  WHICH BRILLIANT GENIOUS decided to make stdin a macro?
 * I mean really, was it really so hard to think about possible consequences?
 * Screw you solaris developers. Screw you.
 */
#undef stdin
#undef stdout
#undef stderr

extern int interactive;

extern char *script;
extern char **script_argv;

/*
 * The process group for our little shell.
 */
static struct rsh_process_group rsh_pgroup;

/*
 * A struct termios for holding the canonical terminal settings for child
 * processes.
 */
static struct termios defaults;

/*
 * This is used to make sure the unnecessary ends of a pipe are closed by both
 * the child and the parent.
 */
int rsh_pipe[2];
int pipe_used = 0;

/*
 * Initialize the process group for the shell. This should only be called once.
 */
int init_rsh_pgroup(int procs){

  if ( procs < 1 )
    return RSH_ERR;

  /* Init the memory. */
  memset(&rsh_pgroup, 0, sizeof(rsh_pgroup));
  rsh_pgroup.pgroup = (struct rsh_process **)
    malloc(sizeof(struct rsh_process *) * procs);
  memset(rsh_pgroup.pgroup, 0, sizeof(struct rsh_process *) * procs);
  
  if ( ! rsh_pgroup.pgroup )
    return RSH_ERR;

  rsh_pgroup.pid = getpid();
  rsh_pgroup.pgid = tcgetpgrp(STDIN_FILENO);
  
  rsh_pgroup.max_procs = procs;

  /* These are the file descriptors for the shells I/O. These are the defaults
   * for child processes barring redirection/pipes. */
  rsh_pgroup.stdin = STDIN_FILENO;
  rsh_pgroup.stdout = STDOUT_FILENO;
  rsh_pgroup.stderr = STDERR_FILENO;

  /* Now we make the first struct rsh_process in the process list reflect the
   * shell itself. */
  struct rsh_process *self = (struct rsh_process *)
    malloc(sizeof(struct rsh_process));
  self->pid = getpid();
  self->pgid = getpid();
  if ( interactive )
    setpgid(self->pid, 0); /* Set the process group of the shell. */
  self->background = 0; /* Its a safe bet we are in the foreground here. */
  self->running = 1; /* Likewise thatw e are running. */
  memcpy(self->name, "rsh", 4);
  self->stdin = STDIN_FILENO;
  self->stdout = STDOUT_FILENO;
  self->stderr = STDERR_FILENO;
  self->command = script;    /* These two fields are for if this is a script */
  self->argv = script_argv;  /* as opposed to an interactive shell.          */

  if ( self->command )
    while ( self->argv[self->argc] != NULL )
      self->argc++;
  
  tcgetattr(STDIN_FILENO, &self->term); /* Save default terminal settings. */
  rsh_pgroup.pgroup[0] = self;

  /* OK, nothing further. */
  return RSH_OK;

}

/*
 * This is used to save the very first terminal settings we see during shell
 * initialization.
 */
void save_default_term_settings(){

  tcgetattr(STDIN_FILENO, &defaults);
  
}

/*
 * Register a pipe, this pipe will then be properly handled by the rsh_exec()
 * code.
 */
void rsh_register_pipe(int pip[2]){

  rsh_pipe[0] = pip[0];
  rsh_pipe[1] = pip[1];

  pipe_used = 1;

}

/*
 * Put the shell in the foreground and then return.
 */
void shell_fg(){

  //printf("Setting process group for shell: %d\n", rsh_pgroup.pgid);
  tcsetpgrp(STDIN_FILENO, rsh_pgroup.pgid);
  tcsetattr(STDIN_FILENO, TCSADRAIN, &rsh_pgroup.pgroup[0]->term);
  rsh_pgroup.pgroup[0]->running = 1;
  rsh_pgroup.pgroup[0]->background = 0;

}

/*
 * Put a process into the background. This is pretty damn simple, actually.
 */
void background(struct rsh_process *proc){

  int err;

  /* Make sure the process actually is running, thats it. */
  if ( ! proc->running ){

    err = kill(-proc->pid, SIGCONT);
    if ( err ){
      perror("kill()");
      return;
    }

    proc->running = 1;

  }

  proc->background = 1;

}

/*
 * Put a process into the foreground. Use tcsetpgid() to force the terminal to
 * pass signals to the specified process. If the process is not currently 
 * running wake it up with a SIGCONT.
 */
int foreground(struct rsh_process *proc){

  int err, status, ret = 0; 

  /* Make the passed procees receive signals from this terminal. */
  tcsetpgrp(STDIN_FILENO, proc->pgid);

  /* Restore the canonical mode terminal settings. */
  tcsetattr(STDIN_FILENO, TCSADRAIN, &defaults);

  /* Make sure the process actually is running. */
  if ( ! proc->running ){

    err = kill(-proc->pid, SIGCONT);
    if ( err ){
      perror("kill()");
      return -1;
    }

    proc->running = 1;

  }

  /* This process is now in the foreground. */
  proc->background = 0;

  /* Now we wait on the process for some info. */
 rewait:
  err = waitpid(proc->pid, &status, WUNTRACED);
  if ( interactive ){
    if ( WIFEXITED(status) ){
      ret = WEXITSTATUS(status);
      cleanup_proc(proc);
      goto shell_foreground;
    }
  } else if ( err == -1 ){
    perror("waitpid()");
    ret = -1;
    goto shell_foreground;
  }

  if ( WIFEXITED(status) ){

    ret = WEXITSTATUS(status);
    cleanup_proc(proc);
    goto shell_foreground;

  } else if ( WIFSIGNALED(status) ){

    if ( interactive )
      printf("\nProcess (%ld) terminated by signal.\n", (long)proc->pid);
    status = WEXITSTATUS(status);
    cleanup_proc(proc);

  } else if ( WIFSTOPPED(status) ){

    /* The signal was stopped, not terminated. */
    proc->running = 0;
    if ( ! interactive )
      goto rewait;
    else
      printf("\nProcess (%ld) stopped [sig=%d].\n", 
	     (long)proc->pid, WSTOPSIG(status));

  } else {

    /* Umm, well, this shouldn't happen. */
    ret = -1;

  }

  /* Make sure the shell is back in the foreground now. */
 shell_foreground:
  shell_fg();

  return ret;

}

/*
 * This is called by the parent of the forked process. It will figure out what
 * to do about background vs foreground stuff, pipe handling, etc.
 */
int _parent_exec(struct rsh_process *proc){

  int status = -1;

  if ( interactive )
    setpgid(proc->pid, proc->pgid);

  if ( proc->pipe_used ){
    if ( proc->pipe_lane == STDIN_FILENO )
      close(proc->pipe[1]);
    else if ( proc->pipe_lane == STDOUT_FILENO || 
	      proc->pipe_lane == STDERR_FILENO )
      close(proc->pipe[0]);
  }

  /* Now based on whether we are an interactive shell and whether the job
   * should be back grounded we either wait or return immediatly. */
  if ( ! proc->background ){
    /* The shell is no longer the foreground. */
    rsh_pgroup.pgroup[0]->background = 1;
    status = foreground(proc);    
  } else {
    /* Put this process into the background and return immediately. */
    background(proc);
  }

  /* We should not get here but eh. */
  return status;

}

/*
 * This is called by the child of the fork() to get the ball rolling for the
 * new process.
 */
int _child_exec(struct rsh_process *proc){

  int err;

  if ( interactive )
    setpgid(proc->pid, proc->pgid);
  
  if ( proc->pipe_used ){

    /* If this pipe is for stdin, then we are reading from the pipe, and as
     * such we do not need the write end. So close it. If this pipe is for
     * stdout or stderr, then we don't need the read end, so close it. */
    if ( proc->pipe_lane == STDIN_FILENO )
      close(proc->pipe[1]);
    else if ( proc->pipe_lane == STDOUT_FILENO || 
	      proc->pipe_lane == STDERR_FILENO )
      close(proc->pipe[0]);
    /* Else: ??? bug... */

  }

  /* Now make the stdin, stderr, stdout file descriptors work. Do this after
   * we foreground or w/e. */
  if ( dup2(proc->stdin, STDIN_FILENO) < 0 ){
    perror("dup()");
    return RSH_ERR;
  }

  if ( dup2(proc->stdout, STDOUT_FILENO) < 0 ){
    perror("dup()");
    return RSH_ERR;
  }

  if ( dup2(proc->stderr, STDERR_FILENO) < 0 ){
    perror("dup()");
    return RSH_ERR;
  }

  /* Reset signal handlers. */
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
  
  /* Now do the exec. Use execve so we don't have to do PATH searching, instead
   * let libc do that for us. */
  fsync(STDOUT_FILENO);
  err = execvp(proc->command, proc->argv);
  if ( err ){
    perror("exec");
  }
  exit(1);

}

/*
 * Internal function where all the gory details of actually spawning the new
 * process occur.
 */
int _do_rsh_exec(struct rsh_process *proc){

  pid_t pid;

  /* First off fork(). */
  pid = fork();
  if ( pid == -1 ){
    perror("fork():");
    return RSH_ERR;
  }

  proc->pid = pid;
  proc->running = 1;
  
  /* 
   * We must perform job control. This is if we are a regular shell, i.e we are
   * typing commands into a terminal. Otherwise, if we are not interactive,
   * think script, we should instead let the parent shell do job control.
   */
  if ( pid ){
    /* Parent. */
    if ( interactive )
      proc->pgid = pid;
    else
      proc->pgid = 0; /* Same group as parent (the shell). */
    
    return _parent_exec(proc);
  } else {
    /* Child.  */
    if ( interactive )
      proc->pgid = getpid(); /* A new process group. */
    else
      proc->pgid = 0;

    return _child_exec(proc);
  }

}

/*
 * This is the whole point of the shell. Initialize a struct rsh_process with
 * the relevant information and then start the process. Nothing terribly
 * complex. We assume that the file descriptors passed are already set up as
 * pipes if that was desired. Other than that, do the fork(), then depending
 * on 'foreground' either wait for the process to finish or just immediatly
 * return.
 */
int rsh_exec(char *command, int argc, char *argv[], int stdin, int stdout,
	     int stderr, int background, int pipe_type, int pipe[2]){

  struct rsh_process *proc;

  /* Get an available struct rsh_process from the table of processes. */
  proc = get_empty_process();
  if ( ! proc )
    return RSH_ERR;

  /* Fill in the process struct. */
  memcpy(proc->name, command, 127);
  proc->name[127] = 0;
  proc->stdin = stdin;
  proc->stdout = stdout;
  proc->stderr = stderr;
  proc->command = command;
  proc->argv = argv;
  proc->argc = argc;
  proc->background = background;
  
  if ( pipe_type == RSH_PIPE_IN ){
    proc->pipe_used = 1;
    proc->pipe_lane = STDIN_FILENO;
    proc->pipe[0] = pipe[0];
    proc->pipe[0] = pipe[0];
  }
  if ( pipe_type == RSH_PIPE_OUT ){
    proc->pipe_used = 1;
    proc->pipe_lane = STDOUT_FILENO;
    proc->pipe[0] = pipe[0];
    proc->pipe[0] = pipe[0];
  }
  if ( pipe_type == RSH_PIPE_ERR ){
    proc->pipe_used = 1;
    proc->pipe_lane = STDERR_FILENO;
    proc->pipe[0] = pipe[0];
    proc->pipe[0] = pipe[0];
  }

  return _do_rsh_exec(proc);

}

void continue_running_procs(){

  int state = 0;
  struct rsh_process *proc;

  while ( (proc = get_next_proc(&state)) != NULL){
    if ( proc->running ){
      kill(proc->pid, SIGCONT);
    }
  }

}

/*
 * Get a process structure and add it to the list of processes. Init the new
 * process struct memory to 0 and return the pointer to it. If this function
 * fails, return NULL.
 */
struct rsh_process *get_empty_process(){

  int i;
  struct rsh_process **tmp, *proc;

  /* Find an open pointer in the process list. */
  for ( i = 0; i < rsh_pgroup.max_procs; i++)
    if ( rsh_pgroup.pgroup[i] == NULL ) break;
  
  /* There were no open process pointers. */
  if ( i == rsh_pgroup.max_procs ){
    tmp = (struct rsh_process **)
      malloc(sizeof(struct rsh_process *) * (rsh_pgroup.max_procs + 8));
    if ( ! tmp )
      return NULL;
    memset(tmp, 0, sizeof(struct rsh_process *) * (rsh_pgroup.max_procs + 8));
    memcpy(tmp, rsh_pgroup.pgroup, 
	   sizeof(struct rsh_process *) *rsh_pgroup.max_procs);
    free(rsh_pgroup.pgroup);
    rsh_pgroup.pgroup = tmp;
    rsh_pgroup.max_procs += 8;
    //printf("Resized process group: %d\n", rsh_pgroup.max_procs);
    //printf("  New address: %p\n", rsh_pgroup.pgroup);
  }

  proc = (struct rsh_process *) malloc(sizeof(struct rsh_process) );
  if ( ! proc )
    return NULL;
  memset(proc, 0, sizeof(struct rsh_process));
  rsh_pgroup.pgroup[i] = proc;

  /* Now we can just return the first of the newly allocated process structs */
  return proc;

}

/*
 * Set *state to zero on the first call. Then simply call this function over
 * and over again until it returns NULL.
 */
struct rsh_process *get_next_proc(int *state){

  struct rsh_process *tmp = NULL;

  /* Iterate across our list. The next non-null element of the process table
   * should be returned. If we run out of processes before we return, then tmp
   * will be NULL, and will signify the end of the process table. */
  for ( ; *state < rsh_pgroup.max_procs; (*state)++){
    if ( rsh_pgroup.pgroup[*state] ){
      tmp = rsh_pgroup.pgroup[*state];
      (*state)++;
      break;
    }
  }

  return tmp;

}

/*
 * Cleanup after a terminated or exited process.
 */
void cleanup_proc(struct rsh_process *proc){

  int i;
  char _eof = 0x04;

  /* Close the processes pipe streams if necessary. */
  if ( proc->pipe_used ){
    close(proc->pipe[0]);
    close(proc->pipe[1]);
  }

  /* Release the process structure's memory. */
  free(proc);

  /* Now find the pointer in the pgroup table that corresponds to this pointer
   * and delete it. */
  for ( i = 0; i < rsh_pgroup.max_procs; i++){
    if ( rsh_pgroup.pgroup[i] == proc )
      rsh_pgroup.pgroup[i] = NULL;
  }

}

/*
 * This is important. This function iterates over all processes that RSH has
 * spawned and does a quick check up. It calls waitpid with the non-blocking
 * option so that we can get immediate feedback on the state of a process. Any
 * process that has terminated in the background will be picked up by this
 * function and cleaned up properly.
 */
void check_processes(){

  int state = 0;
  int status, ret;
  struct rsh_process *proc;
  
  while ( (proc = get_next_proc(&state)) != NULL ){

    /* Don't check ourselves. */
    if ( proc->pid == rsh_pgroup.pid )
      continue;

    /* Don't check foreground processes either, those are dealt with by the
     * foreground() function. */
    if ( ! proc->background )
      continue;

    printf("waitpit: %d\n", proc->pid);
    ret = waitpid(proc->pid, &status, WNOHANG|WUNTRACED);
    if ( ret == 0 )
      continue;

    if ( ret < 0 ){
      continue;
    }

    /* Here we actually have a change in the process' state. */
    if ( WIFEXITED(status) ){

      cleanup_proc(proc);

    } else if ( WIFSIGNALED(status) ){
      
      cleanup_proc(proc);

    } else if ( WIFSTOPPED(status) ){

      proc->running = 0;
 
    } /* Else do nothing. */

  }

}

void _display_proc(struct rsh_process *proc){

  printf("Process struct: (addr=%p)\n", proc);
  printf("  name: %s\n", proc->name);
  printf("  pid: %ld\n", (long)proc->pid);
  printf("  stdin: %d\n", proc->stdin);
  printf("  stdout: %d\n", proc->stdout);
  printf("  stderr: %d\n", proc->stderr);
  printf("  background: %s\n", proc->background ? "yes" : "no");

}

void _display_processes(){

  int i = 0;

  printf("max_procs=%d\n", rsh_pgroup.max_procs);

  for ( ; i < rsh_pgroup.max_procs; i++){
    if ( rsh_pgroup.pgroup[i] )
      _display_proc(rsh_pgroup.pgroup[i]);
  }

}
