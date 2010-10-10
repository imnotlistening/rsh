/**
 * Execute a new process. Either foreground or background. Oh my this is more
 * complex than I had hoped.
 */

#ifndef _EXEC_H
#define _EXEC_H

#include <unistd.h>
#include <termios.h>
#include <sys/types.h>

/* 
 * Pipe types that should be passed to rsh_exec(). These *DO NOT* correspond to
 * the stdin/out/err default descriptor values. Do not be temped to try and
 * pass one to rsh_exec().
 */
#define RSH_PIPE_NONE    0x0
#define RSH_PIPE_IN      0x1
#define RSH_PIPE_OUT     0x2
#define RSH_PIPE_OUT     0x4

/* 
 * Process structure. This is generated by the rsh_exec() function. It holds
 * all relevant information regarding a sub process. 
 */
struct rsh_process {

  /* Process ID and process group ID. */
  pid_t pid;
  gid_t pgid;
  
  /* Process' standard I/O streams. */
  int stdin;
  int stdout;
  int stderr;

  /* Zero if this process is in the foreground. */
  int background;
  
  /* Non-zero if the process is actually running. */
  int running;

  /* The first 128 chars (if there are that many) of the commands actual
   * file name (i.e: /usr/bin/ping). */
  char name[128];

  /* Pipe related stuff. */
  int pipe[2];
  int pipe_used;
  int pipe_lane; /* Which (stdin, stdout, stderr) is being piped. */

  /* Terminal settings. */
  struct termios term;

  /* These will probably not remain valid forever, but as long as they last
   * up to the fork() call, they will be good for the child process to use.
   */
  char *command;
  char **argv;
  int argc;

};

/* 
 * This is a struct defining a list of processes that the shell is aware of. 
 * The shell maintains this list for job control. It also maintains a struct
 * rsh_process describing itself as the first elem.
 */
struct rsh_process_group {

  /* List related stuff. */
  struct rsh_process **pgroup;
  int max_procs;

  /* The standard I/O streams that point to the controlling terminal. Or a 
   * pipe if configured that way, but these should never not be 0, 1, & 2. */
  int stdin;
  int stdout;
  int stderr;
  
  /* The pid of the shell. Why not? */
  pid_t pid;

  /* The process group ID for the shell's process group. */
  gid_t pgid;

};

/* Some relevant functions. */
int                 init_rsh_pgroup(int procs);
int                 rsh_exec(char *command, int argc, char *argv[], int stdin, 
			     int stdout, int stderr, int background, 
			     int pipe_type, int pipe[2]);
int                 foreground(struct rsh_process *proc);
void                background(struct rsh_process *proc);
void                shell_fg();
void                continue_running_procs();
void                save_default_term_settings();

/* Deal with the file descriptors of stdin, out, and err for command.c */
int                 set_stdin(char *file);
int                 set_stdout(char *file, int append);
int                 set_stderr(char *file, int append);

/* Process table functions. Just some boring book keeping. */
struct rsh_process *get_empty_process();
struct rsh_process *get_next_proc(int *state);
void                cleanup_proc(struct rsh_process *proc);
void                check_processes();
/* Debugging process table functions. */
void                _display_processes();

#endif