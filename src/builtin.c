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

#include <math.h>
#include <time.h>
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
int builtin_mkdir(int argc, char **argv, int in, int out, int err);
int builtin_pwd(int argc, char **argv, int in, int out, int err);
int builtin_cat(int argc, char **argv, int in, int out, int err);
extern int builtin_df(int argc, char **argv, int in, int out, int err);

/* Native commands that really shouldn't be covered up by the built in FS. */
int builtin_ncp(int argc, char **argv, int in, int out, int err);
int builtin_ndf(int argc, char **argv, int in, int out, int err);

/* This is defined in source.c */
extern int builtin_source(int argc, char **argv, int in, int out, int err);

/* Defined in fs_fat16.c */
extern int builtin_fatinfo(int argc, char **argv, int in, int out, int err);

/* Defined in rshio.c */
extern int builtin_native(int argc, char **argv, int in, int out, int err);

/* Defined in fs.c */
extern int builtin_dumpfds(int argc, char **argv, int in, int out, int err);

/* Builtin function storage. */
struct builtin builtins[] = {

  {"exit", builtin_exit},
  {"cd", builtin_cd},
  {"exec", builtin_exec},
  {"history", builtin_history},
  {"fg", builtin_fg},
  {"bg", builtin_bg},
  {"dproc", builtin_dproc},
  {"dfs", builtin_dfs},
  {"fatinfo", builtin_fatinfo},
  {"source", builtin_source},
  {"export", builtin_export},
  {"native", builtin_native},
  {"dumpfds", builtin_dumpfds},
  {"ncp", builtin_ncp},
  {"ndf", builtin_ndf},
  {"xfer", builtin_xfer},
  {"cp", builtin_cp},
  {"df", builtin_df},
  {"mv", builtin_mv},
  {"ls", builtin_ls},
  {"rm", builtin_rm},
  {"touch", builtin_touch},
  {"mkdir", builtin_mkdir},
  {"cat", builtin_cat},
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
    ret = rsh_chdir(*argv);
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

/* Data for our copy command. */
int list_elems;
int list_length;
char **list;
#define _LIST_INC_SIZE 8

int _rsh_cp_listadd(char *item){

  char **tmp;

  /* Make sure there is enough space */
  if ( list_elems >= list_length ){
    tmp = (char **)malloc(sizeof(char **) * (list_length + _LIST_INC_SIZE));
    if ( list ){
      memcpy(tmp, list, sizeof(char **) * list_length);
      free(list);
    }
    list = tmp;
    list_length += 8;
  }

  /* Now just add the pointer to the list. */
  list[list_elems++] = item;

  return 0;

}

char *_combine_paths(char *dir, char *name){

  char *new_string = (char *)malloc(strlen(dir) + strlen(name) + 1);
  memcpy(new_string, dir, strlen(dir));
  memcpy(new_string + strlen(dir), name, strlen(name)+1);
  return new_string;

}

int _rsh_cp_bifs_expand(char *star_path){

  int dfd, fd;
  char *combined;
  struct stat statbuf;
  struct dirent *dent;

  dfd = rsh_open(star_path, 0, 0);
  if ( ! dfd ){
    perror("rsh_open");
    printf("ARGGG couldn't open: %s\n", star_path);
    return RSH_ERR;
  }

  while ( (dent = rsh_readdir(dfd)) != NULL ){

    combined = _combine_paths(star_path, dent->d_name);

    fd = rsh_open(combined, 0, 0);
    if ( fd < 0 ){
      printf("I/O error on %s, skipping.\n", dent->d_name);
      continue;
    }

    if ( rsh_fstat(fd, &statbuf) != 0 ){
      perror("rsh_fstat");
      printf("IO error on %s, skipping\n", dent->d_name);
      rsh_close(fd);
      continue;
    }

    /* Skip directories, recursive copying is a lot of work. */
    if ( ! S_ISREG(statbuf.st_mode) ){
      printf("Skipping non-regular file: %s\n", dent->d_name);
      rsh_close(fd);
      continue;
    }

    _rsh_cp_listadd(combined);

    rsh_close(fd);

  }
  if ( errno ){
    perror("readdir");
    return RSH_ERR;
  }
  rsh_close(dfd);

  return 0;

}

int _rsh_cp_native_expand(char *star_path){

  DIR *dfd;
  char *combined;
  struct stat statbuf;
  struct dirent *dent;

  /* Now we use readdir to figure this shit out. */
  dfd = opendir(star_path);
  if ( ! dfd ){
    perror("(native) opendir");
    printf("ARGGG couldn't open: %s\n", star_path);
    return RSH_ERR;
  }

  while ( (dent = readdir(dfd)) != NULL ){

    combined = _combine_paths(star_path, dent->d_name);

    if ( lstat(combined, &statbuf) ){
      perror("(native) lstat");
      printf("IO error on %s, skipping\n", dent->d_name);
      continue;
    }

    /* Skip directories, recursive copying is a lot of work. */
    if ( ! S_ISREG(statbuf.st_mode) ){
      printf("Skipping non-regular file: %s\n", dent->d_name);
      continue;
    }

    _rsh_cp_listadd(combined);

  }
  if ( errno ){
    perror("readdir");
    return RSH_ERR;
  }
  closedir(dfd);

  return 0;

}

int _rsh_cp_star_expand(char *_star_path){

  int ret;
  char star[] = {'.', '/', '*', 0}; /* This makes sense later on... Really. */
  char *star_path;

  if ( strcmp("*", _star_path) == 0 )
    star_path = star;
  else
    star_path = _star_path;

  /* Chop the trailing star off. */
  star_path[strlen(star_path)-1] = 0;

  /* Now, if the path is native, then use the native expand, if its a built in
   * then use the builtin expansion. The reason this must be done is becuase
   * the builtin readdir sematics are slightly different from the native libc
   * semantics. My bad.
   */
  if ( rsh_native_path(star_path) )
    ret = _rsh_cp_native_expand(star_path);
  else
    ret = _rsh_cp_bifs_expand(star_path);

  return ret;

}

#define CHUNK_SIZE (16*1024) /* Copy 16KB chunks at a time. */

/*
 * This function assumes erorr cheching has been done so far. Thus we can
 * expect that if dest is a directory, we are copying source _into_ dest and
 * if dest is a regular file, then we are overwriting dest with source.
 */
int _do_copy(char *source, char *dest){

  int ret = 0;
  int bytes;
  int tmpfd;
  int dest_fd;
  int source_fd;
  int dest_type; /* 0 = file, non-0 = directory. */
  int free_tmp = 0, free_dest_file_name = 0;
  char *tmp, *source_node;
  char *dest_file_name;
  char chunk[CHUNK_SIZE];
  struct stat statbuf;

  /* Dest FD is the hard one so lets do that first. */
  if ( rsh_native_path(dest) ){
    if ( lstat(dest, &statbuf) ){
      perror("(native) lstat");
      return RSH_ERR;
    }
  } else {
    if ( *dest == '/' ){
      dest++;
      while ( *dest++ != '/' );
    }
    tmpfd = rsh_open(dest, 0, 0);
    if ( tmpfd < 0 ){
      perror("rsh_open");
      printf("Unable to open %s.\n", dest);
      return RSH_ERR;
    }
    rsh_fstat(tmpfd, &statbuf);
  }
  dest_type = statbuf.st_mode & S_IFDIR;

  if ( dest_type != 0 ){ /* Directory. */
    if ( dest[strlen(dest)-1] != '/' ){ /* Tack on a '/' if necessary. */
      tmp = malloc(strlen(dest) + 2);
      memcpy(tmp, dest, strlen(dest));
      tmp[strlen(dest)] = '/';
      tmp[strlen(dest)+1] = 0;
      free_tmp = 1;
    }
    /* Make sure we only get the last of the nodes from source. */
    source_node = source + strlen(source);
    while ( source_node > source ){
      if ( *source_node == '/' ){
	source_node++;
	break;
      }
      source_node--;
    }
    dest_file_name = _combine_paths(dest, source_node);
    free_dest_file_name = 1;
  } else {
    dest_file_name = dest;
  }

  source_fd = rsh_open(source, O_RDONLY, 0);
  if ( source_fd < 0 ){
    perror("rsh_open");
    printf("Unable to open source file.\n");
    ret = RSH_ERR;
    goto cleanup;
  }

  dest_fd = rsh_open(dest_file_name, O_WRONLY|O_CREAT,
	     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if ( dest_fd < 0 ){
    perror("rsh_open");
    printf("Unable to open destination file.\n");
    ret = RSH_ERR;
    goto cleanup;
  }

  /* If we are here, we have two valid descriptors. Simply read from source and
   * write to dest until there is nothing left. */
  while ( (bytes = rsh_read(source_fd, chunk, CHUNK_SIZE)) != 0 )
    rsh_write(dest_fd, chunk, bytes);

  /* And we are done. */
 cleanup:
  if ( free_tmp )
    free(tmp);
  if ( free_dest_file_name )
    free(dest_file_name);
  return ret;

}

int builtin_cp(int argc, char **argv, int in, int out, int err){

  int i;
  int fd;
  int ret = 0, tmpret;
  char *dest;
  struct stat buf;
  
  /*
   * First we have to do * expansion. We wont be getting * as a regular
   * expression so *.txt is not something we have to worry about. However, if
   * a * is passed, we must turn that into all files/dirs in our current 
   * working directory, or the absolute path directory. Once we have expanded
   * the possible * characters, we must then figure out how to go about using
   * each of the passed files. Essentially any copy command looks like the
   * following: cp <sources> <dest>. sources may be many files/dirs thus for
   * each passed file, a particular action must be taken, be that a native
   * copy, a native to bifs copy, etc. Anyway, thats the jist of this code.
   */
  argc--;
  argv++;

  if ( argc < 2 ){
    printf("cp: usage: cp <source [source] ...> <dest>\n");
    return 1;
  }

  /* Get the list of source files ready for biznis. */
  list_elems = 0;
  list_length = 0;
  list = NULL;

  /* The destination. */
  dest = argv[argc-1];

  /* Iterate over all passed files. If there is a * then do * expansion. */
  for ( i = 0; i < argc-1; i++ ){
    
    if ( argv[i][strlen(argv[i])-1] == '*' )
      tmpret = _rsh_cp_star_expand(argv[i]);
    else 
      tmpret = _rsh_cp_listadd(argv[i]);

    if ( tmpret < 0 )
      rsh_dprintf(err, "Error expanding %s, ignoring.\n", argv[i]);
    
  }

  /* Make sure dest exists. */
  if ( rsh_native_path(dest) ){
    if ( lstat(dest, &buf) ){
      perror("(native) lstat");
      rsh_dprintf(err, "Unable to (native) stat: %s\n", dest);
      ret = 1;
      goto done;
    }
  } else {
    fd = rsh_open(dest, O_CREAT, 0);
    if ( fd < 0 ){
      perror("rsh_open");
      rsh_dprintf(err, "Unable to (builtin) stat: %s\n", dest);
      ret = 1;
      goto done;
    }
    rsh_fstat(fd, &buf);
    rsh_close(fd);
  }

  /* Make sure we are not about to try and copy multiple files into 1 file. 
   * Instead we must copy multiple files into a directory.
   */
  if ( list_elems > 1 ){
    if ( ! (buf.st_mode & S_IFDIR) ){
      printf("Destination is not a directory.\n");
      ret = 1;
      goto done;
    }
  }

  /* OK, now, basically, we can just copy each part of list into the dest. */
  for ( i = 0; i < list_elems; i++){
    rsh_dprintf(out, "cp: %s -> %s\n", list[i], dest);
    _do_copy(list[i], dest);
  }

 done:
  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);

  return ret;

}

/*
 * ALERT: ***EXTREMELY*** naive implementation of mv. Copy the file, then
 * delete the original. This could be made a billion, million, googlyplex times
 * as fast by implementing it on the dirent level in the file system. But I
 * don't have time to go and pull that sort of code out of my butt :(. This
 * will have to do for now. Plus it allows for copying from one file system to
 * another.
 */
int builtin_mv(int argc, char **argv, int in, int out, int err){

  int ret = 0;
  char *cp_argv[4];

  if ( argc != 3 ){
    rsh_dprintf(err, "Usage: mv <source> <dest>\n");
    return 1;
  }

  cp_argv[0] = "cp";
  cp_argv[1] = argv[1];
  cp_argv[2] = argv[2];
  cp_argv[3] = NULL; /* Null terminate, why not. */

  ret = builtin_cp(3, cp_argv, in, out, err);
  if ( ret )
    goto cleanup;

  if ( rsh_unlink(argv[1]) )
      perror("rsh_unlink");

 cleanup:
  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);
  return ret;

}


int _display_dirent(struct dirent *ent, struct stat *buf, 
		    int bwidth, int swidth, int out){

  char format[32];
  char *file_ctime;

  /* Directory or regular file... */
  if ( buf->st_mode & S_IFDIR )
    rsh_dprintf(out, "d");
  else 
    rsh_dprintf(out, "-");

  /* Permissions. */
  if ( buf->st_mode & S_IRUSR )
    rsh_dprintf(out, "r");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IWUSR )
    rsh_dprintf(out, "w");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IXUSR )
    rsh_dprintf(out, "x");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IRGRP )
    rsh_dprintf(out, "r");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IWGRP )
    rsh_dprintf(out, "w");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IXGRP )
    rsh_dprintf(out, "x");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IROTH )
    rsh_dprintf(out, "r");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IWOTH )
    rsh_dprintf(out, "w");
  else 
    rsh_dprintf(out, "-");

  if ( buf->st_mode & S_IXOTH )
    rsh_dprintf(out, "x");
  else 
    rsh_dprintf(out, "-");

  /* And the file's block size + actual size in bytes. */
  sprintf(format, " %%-%dd %%-%dd ", bwidth, swidth);
  rsh_dprintf(out, format, buf->st_blocks, buf->st_size);

  file_ctime = ctime(&buf->st_ctime);
  file_ctime[strlen(file_ctime)-1] = 0; /* Chop the \n off. */
  rsh_dprintf(out, "%s ", file_ctime);
  rsh_dprintf(out, "%s\n", ent->d_name);

  return RSH_OK;

}

int _rsh_compare_dirent(const void *a, const void *b){

  return strcmp(((struct dirent *)a)->d_name, ((struct dirent *)b)->d_name);

}

int builtin_ls(int argc, char **argv, int in, int out, int err){

  int i;
  int dfd, fd, ret;
  int blocks = 0;
  int swidth = 0;
  int bwidth = 0;
  float l_swidth;
  float l_bwidth;
  int list_len = 0;
  int list_index = 0;
  struct stat *bufs = NULL;
  struct dirent *dirent;
  struct dirent *ent_list = NULL, *tmp;

  if ( native ){
    ret = _native_command(argc, argv, in, out, err);
    goto _cleanup;
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
  
  /* Fill up our dirent list. */
  while ( (dirent = rsh_readdir(dfd)) != NULL ){

    /* Make sure there is enough space in ent_list */
    if ( list_index >= list_len ){
      list_len += 16;
      tmp = (struct dirent *)malloc(sizeof(struct dirent) * list_len);
      if ( ! tmp ){
	ret = RSH_ERR;
	goto cleanup;
      }

      memcpy(tmp, ent_list, sizeof(struct dirent) * (list_len - 16));
      if ( ent_list )
	free(ent_list);
      ent_list = tmp;

    }

    ent_list[list_index] = *dirent;

    list_index++;

  }
  if ( errno ){
    perror("rsh_readdir");
    ret = RSH_ERR;
    goto cleanup;
  }

  /* Now sort the list of dirents. */
  qsort(ent_list, list_index, sizeof(struct dirent), _rsh_compare_dirent);
  
  /* Fill up the stat bufs. */
  bufs = (struct stat *)malloc(sizeof(struct stat) * list_index);
  if ( ! bufs ){
    ret = RSH_ERR;
    goto cleanup;
  }

  for ( i = 0; i < list_index; i++){
    fd = rsh_open(ent_list[i].d_name, 0, 0);
    if ( fd < 0 ){
      perror("rsh_open");
      ret = RSH_ERR;
      goto cleanup;
    }

    ret = rsh_fstat(fd, &(bufs[i]));
    if ( ret < 0 ){
      perror("rsh_stat");
      ret = RSH_ERR;
      goto cleanup;
    }
    rsh_close(fd);

    /* Some formatting calculations. */
    l_bwidth = log10f(bufs[i].st_blocks+1) + 1;
    l_swidth = log10f(bufs[i].st_size+1) + 1;
    if ( l_bwidth > bwidth )
      bwidth = (int) l_bwidth;
    if ( l_swidth > swidth )
      swidth = (int) l_swidth;

    blocks += bufs[i].st_blocks;

  }

  /* Now display each dir entry. */
  for ( i = 0; i < list_index; i++)
    _display_dirent(&(ent_list[i]), &(bufs[i]), bwidth, swidth, out);

 cleanup:
  rsh_close(dfd);
  if ( ent_list )
    free(ent_list);
  if ( bufs )
    free(bufs);
 _cleanup:
  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);

  return ret;

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

  int i;
  int ret = 0;

  printf("builtin: %s\n", *argv);

  if ( native ){
    ret = _native_command(argc, argv, in, out, err);
    goto cleanup;
  }

  /* OK, delete a file off the built in file system. */
  argc--;
  argv++;

  /* Cat each passed arg, if possible */
  for ( i = 0; i < argc; i++){

    if ( rsh_unlink(argv[i]) )
      perror("rsh_unlink");

  }

 cleanup:
  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);
  return ret;

}

int builtin_mkdir(int argc, char **argv, int in, int out, int err){

  int i;

  printf("builtin: %s\n", *argv);
  
  if ( native ){
    _native_command(argc, argv, in, out, err);
    goto cleanup;
  }

  /* Ignore command name. */
  argc--;
  argv++;

  /* Make a dir for each passed arg. Either relative path or absolute path. */
  for ( i = 0; i < argc; i++){
    rsh_mkdir(argv[i], 0); /* mode is meaningless to the naitve FS. */
  }

 cleanup:
  return RSH_OK;

}

/*
 * Call through to the native copy. This is dangerous though since it runs in
 * the shell's context.
 */
int builtin_ncp(int argc, char **argv, int in, int out, int err){

  /* Hack alert. */
  argv[0][0] = 'c';
  argv[0][0] = 'p';
  argv[0][0] = 0;

  return _native_command(argc, argv, in, out, err);

}

int builtin_ndf(int argc, char **argv, int in, int out, int err){

  printf("%s\n", argv[0]);

  /* Hack alert. */
  argv[0][0] = 'd';
  argv[0][0] = 'f';
  argv[0][0] = 0;

  return _native_command(argc, argv, in, out, err);

}


int builtin_cat(int argc, char **argv, int in, int out, int err){

  int i;
  int fd;
  int ret;
  int bytes;
  char chunk[CHUNK_SIZE];
  struct stat buf;

  if ( native ){
    ret = _native_command(argc, argv, in, out, err);
    goto cleanup;
  }
  
  /* Ignore the name of the calling function... We already know it. */
  argc--;
  argv++;

  /* Cat each passed arg, if possible */
  for ( i = 0; i < argc; i++){

    fd = rsh_open(argv[i], O_RDONLY, 0);
    if ( fd < 0 ){
      perror("rsh_open()");
      rsh_dprintf(out, "Error opening '%s' (skipping).\n", argv[i]);
      continue;
    }

    if ( rsh_fstat(fd, &buf) ){
      perror("rsh_fstat");
      rsh_dprintf(out, "Error statting '%s' (skipping).\n", argv[i]);
      continue;
    }

    if ( ! (buf.st_mode & S_IFREG) ){
      rsh_dprintf(out, "Skipping non-regular file: %s\n", argv[i]);
      continue;
    }

    /* Print out the file. Yeesh. */
    while ( ( bytes = rsh_read(fd, chunk, CHUNK_SIZE)) != 0 )
      rsh_write(out, chunk, bytes);

  }

 cleanup:
  RSH_BUILTIN_CLOSE(in);
  RSH_BUILTIN_CLOSE(out);
  RSH_BUILTIN_CLOSE(err);
  return ret;

}
