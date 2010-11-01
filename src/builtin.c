/*
 * Built in commands are located here. Each built in is a function that acts
 * a bit like a main(). It runs in the context of the shell, so don't do any
 * thing crazy.
 *
 * Each command should be placed in the builtins array. This is to facilitate
 * other code finding said builtins. See the builtin.h code for what these
 * structs look like.
 */

#include <rsh.h>
#include <exec.h>
#include <rshio.h>
#include <rshfs.h>
#include <lexxer.h>
#include <builtin.h>
#include <symbol_table.h>

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

/* Prototypes for the builtins. */
int builtin_cd(int argc, char **argv, int in, int out, int err);
int builtin_exit(int argc, char **argv, int in, int out, int err);
int builtin_exec(int argc, char **argv, int in, int out, int err);
int builtin_history(int argc, char **argv, int in, int out, int err);
int builtin_hstack(int argc, char **argv, int in, int out, int err);
int builtin_fg(int argc, char **argv, int in, int out, int err);
int builtin_bg(int argc, char **argv, int in, int out, int err);
int builtin_dproc(int argc, char **argv, int in, int out, int err);
int builtin_dfs(int argc, char **argv, int in, int out, int err);
int builtin_export(int argc, char **argv, int in, int out, int err);
int builtin_xfer(int argc, char **argv, int in, int out, int err);

/* These built ins are wrappers for the equivalent commands. They allow us to
 * (relatively transparently) use the built in file system... */
int builtin_cp(int argc, char **argv, int in, int out, int err);
int builtin_mv(int argc, char **argv, int in, int out, int err);
int builtin_ls(int argc, char **argv, int in, int out, int err);
int builtin_touch(int argc, char **argv, int in, int out, int err);
int builtin_rm(int argc, char **argv, int in, int out, int err);

/* This is defined in source.c */
extern int builtin_source(int argc, char **argv, int in, int out, int err);

/* Defined in fs_fat16.c */
extern int builtin_fatinfo(int argc, char **argv, int in, int out, int err);

/* Defined in rshio.c */
extern int builtin_native(int argc, char **argv, int in, int out, int err);

/* Builtin function storage. */
struct builtin builtins[] = {

  {"exit", builtin_exit},
  {"cd", builtin_cd},
  {"exec", builtin_exec},
  {"history", builtin_history},
/*{"hstack", builtin_hstack}, */ /* This was for debugging. */
  {"fg", builtin_fg},
  {"bg", builtin_bg},
  {"dproc", builtin_dproc},
  {"dfs", builtin_dfs},
  {"fatinfo", builtin_fatinfo},
  {"source", builtin_source},
  {"export", builtin_export},
  {"native", builtin_native},
  {"xfer", builtin_xfer},
  {"cp", builtin_cp},
  {"mv", builtin_mv},
  {"ls", builtin_ls},
  {"rm", builtin_rm},
  {"touch", builtin_touch},
  {NULL, NULL}, /* Null terminate the table. */

};

/*
 * See if we have the requested built in command.
 */
struct builtin *rsh_identify_builtin(char *command){

  struct builtin *bin = builtins;

  /* Iterate across the builtin array and look for the command. */
  while ( bin->name ){
    if ( strcmp(command, bin->name) == 0) /* A match */
      break;
    bin++;
  }
  
  /* If bin->name is not null, we found a match. */
  if ( bin->name )
    return bin;
  else
    return NULL;

}

int builtin_exit(int argc, char **argv, int in, int out, int err){

  /* This should be made a bit more sophisticated... */
  exit(0);
  return 0;

}

/*
 * Change the current working directory.
 */
int builtin_cd(int argc, char **argv, int in, int out, int err){

  int ret;

  /* We know we are 'cd'. */
  argc--;
  argv++;

  /* Now check to see if there is 0 or 1 arguments passed. */
  if ( argc == 1 ){
    /* Try and set the cwd... */
    ret = chdir(*argv);
    if ( ret )
      perror("cd");
  } else if ( argc == 0 ){
    /* Get $HOME, and cd to $HOME. */
    
  } else {
    printf("cd: invalid usage.\n");
    ret = -1;
  }

  return -ret;

}

int builtin_exec(int argc, char **argv, int in, int out, int err){

  /*
   * Make an exec syscall. Most shells really just use this for doing fd
   * manipulation. Not implemented yet.
   */

  return 0;

}

extern struct rsh_history_stack hist_stack;
extern char **history;
int builtin_hstack(int argc, char **argv, int in, int out, int err){

  struct rsh_history_frame *tmp;

  if ( ! hist_stack.top ){
    printf("No history stack.\n");
    return 0;
  }

  tmp = hist_stack.top;
  while ( tmp != NULL ){

    printf("%p: %d -> %s\n", tmp, tmp->text, history[tmp->text]);
    tmp = tmp->next;

  }

  return 0;

}

int builtin_history(int argc, char **argv, int in, int out, int err){

  rsh_history_print();
  return 0;

}

int builtin_fg(int argc, char **argv, int in, int out, int err){

  /* Chop of the command. We don't care about it. */
  ++argv;
  --argc;

  /* Now, we just try and foreground the top of the process group queue. That
   * is to say the last of the processg group queue.
   */
  int state = 0;
  struct rsh_process *proc = NULL;
  struct rsh_process *fg_proc = NULL;
  
  while ( (proc = get_next_proc(&state)) != NULL ){
    if ( ! proc->running )
      fg_proc = proc;
  }

  /* No processes to foreground. */
  if ( ! fg_proc ){
    printf("No process to foreground.\n");
    return 0;
  }

  /* fg_proc is the last process to come out of the process list that is not
   * running. We will run it in the foreground here. */
  return foreground(fg_proc);

}

int builtin_bg(int argc, char **argv, int in, int out, int err){

  /* Chop of the command. We don't care about it. */
  ++argv;
  --argc;

  /* Now, we just try and foreground the top of the process group queue. That
   * is to say the last of the processg group queue.
   */
  int state = 0;
  struct rsh_process *proc;
  struct rsh_process *bg_proc;
  
  while ( (proc = get_next_proc(&state)) != NULL ){
    if ( ! proc->running )
      bg_proc = proc;
  }

  /* No processes to foreground. */
  if ( ! bg_proc ){
    printf("No process to background.\n");
    return 0;
  }

  /* fg_proc is the last process to come out of the process list that is not
   * running. We will run it in the background here. */
  background(bg_proc);
  return 0;

}

/*
 * Display the list of processes that RSH is aware of (child processes).
 */
int builtin_dproc(int argc, char **argv, int in, int out, int err){

  int state = 0;
  struct rsh_process *proc;
  
  while ( (proc = get_next_proc(&state)) != NULL ){

    printf("pid %-5ld (%s): %s\n", (long)proc->pid, 
	   proc->running ? "running" : "stopped", proc->name);

  }

  return 0;

}

/*
 * Export a variable in our symbol table to the environment. 
 */
int builtin_export(int argc, char **argv, int in, int out, int err){

  char *symdata;

  /* Ignore the name of the command. */
  argc--;
  argv++;

  /* Now attempt to export each passed argument. */
  while ( *argv ){

    symdata = symtable_get(*argv);
    setenv(*argv, symdata, 1);

    argv++;

  }

  return RSH_OK;

}

/*
 * For now just a test of piping from a command to a built in.
 */
int builtin_xfer(int argc, char **argv, int in, int out, int err){

  int bytes;
  char buf[128];

  while ( (bytes = rsh_read(in, buf, 127)) > 0 ){
    buf[bytes] = 0;
    rsh_dprintf(out, "%s", buf);
  }

  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);

  return 0;

}

/* 
 * Basically we just need to list the registered file system and some
 * internal information. `df' will be done through a similar mechanic but
 * this particular function is meant to be more for debugging. 
 */
extern struct rsh_file_system fs;
extern char *rsh_cwd;
int builtin_dfs(int argc, char **argv, int in, int out, int err){

  rsh_dprintf(out, "CWD: %s\n", rsh_cwd);
  rsh_dprintf(out, "FS info:\n");
  rsh_dprintf(out, "  ftable addr: 0x%016lx\n", (long unsigned int)fs.ftable);
  rsh_dprintf(out, "  ftable length: %d, used %d\n", fs.ft_length, fs.used);
  rsh_dprintf(out, "  File ops:\n");
  rsh_dprintf(out, "    read:  0x%016lx\n", (long unsigned int)fs.fops->read);
  rsh_dprintf(out, "    write: 0x%016lx\n", (long unsigned int)fs.fops->write);
  rsh_dprintf(out, "    open:  0x%016lx\n", (long unsigned int)fs.fops->open);
  rsh_dprintf(out, "    close: 0x%016lx\n", (long unsigned int)fs.fops->close);

  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);  

  return 0;

}

extern int native;

/*
 * Run a command natively. This deliberatly by passes rsh_exec() since we do
 * not want to call rsh_exec() when we should be running in context of the
 * shell. This will do a waitpid while the process runs. It is roughly 
 * equivalent to the system() library routine.
 */
int _native_command(int argc, char **argv, int in, int out, int err){

  int ret;
  int status;
  pid_t pid;

  pid = fork();

  if ( pid < 0 ){
    perror("fork");
    return -1;
  }

  /* Parent. */
  if ( pid ){
    ret = waitpid(pid, &status, 0);
    if ( ret < 0 ){
      perror("waitpid");
      return -1;
    }
    return WEXITSTATUS(status);
  }

  /* We are the child. */
  if ( dup2(in, STDIN_FILENO) < 0 ){
    perror("dup()");
    return RSH_ERR;
  }

  if ( dup2(out, STDOUT_FILENO) < 0 ){
    perror("dup()");
    return RSH_ERR;
  }

  if ( dup2(err, STDERR_FILENO) < 0 ){
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
  err = execvp(*argv, argv);
  if ( err ){
    perror("exec");
  }
  exit(1);

}

int builtin_cp(int argc, char **argv, int in, int out, int err){

  printf("builtin: %s\n", *argv);
  
  if ( native ){
    return _native_command(argc, argv, in, out, err);
  }

  return 0;

}

int builtin_mv(int argc, char **argv, int in, int out, int err){

  printf("builtin: %s\n", *argv);
  
  if ( native ){
    return _native_command(argc, argv, in, out, err);
  }
  
  return 0;

}

int builtin_ls(int argc, char **argv, int in, int out, int err){

  int dfd;

  printf("builtin: %s\n", *argv);

  if ( native ){
    _native_command(argc, argv, in, out, err);
    goto cleanup;
  }

  argc--;
  argv++;

  /* Otherwise, we have to do this ourselves. */
  if ( ! *argv )
    dfd = rsh_open(".", 0, 0);
  else 
    dfd = rsh_open(*argv, 0, 0);

  if ( dfd < 0 ){
    perror("open");
    return 1;
  }

  struct dirent *dirent;
  while ( (dirent = rsh_readdir(dfd)) != NULL ){
    printf("> name: %s\n", dirent->d_name);
  }
  if ( errno ){
    perror("_rsh_readdir");
    return 1;
  }

 cleanup:
  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);

  return 0;

}

/*
 * Basically just open and close a file. Not hard really.
 */
int builtin_touch(int argc, char **argv, int in, int out, int err){

  int fd;

  printf("builtin: %s\n", *argv);

  if ( native ){
    _native_command(argc, argv, in, out, err);
    goto cleanup;
  }

  /* Ignore the name of the calling function... We already know it. */
  argc--;
  argv++;

  /* The magic happens here. */
  rsh_dprintf(out, "Touching file: %s\n", *argv);
  fd = rsh_open(*argv, O_CREAT, 0);
  if ( fd < 0 ){
    perror("rsh_open");
    return 1;
  }

  rsh_close(fd);

 cleanup:
  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);

  return 0;

}

int builtin_rm(int argc, char **argv, int in, int out, int err){

  printf("builtin: %s\n", *argv);

  if ( native ){
    _native_command(argc, argv, in, out, err);
  }

  return 0;

}
