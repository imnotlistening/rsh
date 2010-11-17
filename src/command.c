/**
 * Handles the execution of different types of statements. For instance 
 * rsh_command() can execute a command, do a symbol assignment, etc. Since we
 * assume the sequence of tokens we are passed as a command has been 
 * preparsed() by the parser, executing a given statement should at this point
 * (hopefully) be as simple as reading a command from left to right, building
 * the exec environment, and then hitting start (eh, well, sort of).
 */

#include <rsh.h>
#include <exec.h>
#include <parser.h>
#include <lexxer.h>
#include <builtin.h>
#include <symbol_table.h>

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

int command_state = S_BASE;

/* These variables are for the current command being executed. They relate 
 * to the argument list being passed to the command. */
char *command = NULL;
char **argv = NULL;
int argc = 0;
int max_argc = 0;

/* Base I/O descriptors for each dispatched process. */
int proc_stdin = 0, proc_stdout = 1, proc_stderr = 2;

/* 
 * Pipe status: if this is not set and we are looking to dispatch a command, no
 * pipe was used, if this is set, the the previous command is piping output to
 * the current commands input. If this is not set and there is a pipe request,
 * then set this variable.
 */
int pipe_status = 0;

/*
 * The pipe itself.
 */
int pipe_fds[2];

/**
 * Dispatch a process via rsh_exec(). This will either background or foreground
 * the new process based on 'interactive'.
 */
int dispatch_process(int background){

  int status;
  struct builtin *bin;

  /* Handle built in commands. */
  bin = rsh_identify_builtin(command);
  if ( bin ){

    status = bin->func(argc, argv, proc_stdin, proc_stdout, proc_stderr);
    clear_argv();
    return status;

  }

  status = rsh_exec(command, argc, argv, 
		    proc_stdin, proc_stdout, proc_stderr, background,
		    pipe_status, pipe_fds);
  clear_argv();
  return status;

}

/**
 * Execute a seq of tokens.
 * 
 * Note:
 *   Based on each token and the shell state we can take an action. For 
 * instance if we get a WORD token and we are in the S_BASE then we know
 * that this word is a command name. Similar actions can be taken for
 * all of the rest of the tokens as well. Thus execution of a command is
 * basically a state machine.
 */
int rsh_command(struct rsh_token *seq){

  int ret;

  clear_argv();

  /*
   * Iterate over the entire command.
   */
  while ( 1 ){

    switch (seq->type){

      /*
       * In the case of a WORD, we should first see, are we state S_COMMAND?
       * If we are, this is an arg. Otherwise it is the actual command itself.
       */
    case WORD:
      if ( ! seq->tok )
	break;
      if ( command_state == S_COMMAND ){
	append_to_argv(seq->tok);
      } else {
	command = strdup(seq->tok);
	append_to_argv(seq->tok);   /* Command name is first arg. */
	command_state = S_COMMAND;
      }
      break;
      
      /*
       * This is a symbol definition. We don't have to worry about a symdef
       * being in an odd place (like in the arg list) since that was handled
       * by the preparsing. In any event, chop the equals sign off, and if
       * the next token is a word do a symbol assignment.
       */
    case SYMDEFINITION:
      seq->tok[strlen(seq->tok)-1] = 0; /* Chop the '=' */

      if ( (seq+1)->type == WORD ){
	symtable_add(seq->tok, (seq+1)->tok);
	seq++;
      } else {
	symtable_add(seq->tok, NULL);
      }

      break;

      /*
       * Change this commands stdin to be from somewhere else.
       */
    case REDIRECT_IN:
      seq++; /* We need to know where to redirect from. */
      if ( seq->type == WHITESPACE )
	seq++;

      if ( seq->type == NULL_LEX ){
	printf("Syntax error: missing redirect destination.\n");
	return RSH_ERR;
      }

      if ( set_stdin(seq->tok) == RSH_ERR )
	return RSH_ERR;

      break;

      /*
       * Change this commands stdout to be to somewhere else.
       */
    case REDIRECT_OUT:
      seq++; /* We need to know where to redirect from. */
      if ( seq->type == WHITESPACE )
	seq++;

      if ( seq->type == NULL_LEX ){
	printf("Syntax error: missing redirect destination.\n");
	return RSH_ERR;
      }

      if ( set_stdout(seq->tok, 0) == RSH_ERR )
	return RSH_ERR;

      break;

      /*
       * Change this commands stderr to be to somewhere else.
       */
    case REDIRECT_ERR:
      seq++; /* We need to know where to redirect from. */
      if ( seq->type == WHITESPACE )
	seq++;

      if ( seq->type == NULL_LEX ){
	printf("Syntax error: missing redirect destination.\n");
	return RSH_ERR;
      }

      if ( set_stderr(seq->tok, 0) == RSH_ERR )
	return RSH_ERR;

      break;
      /*
       * Change this commands stdout to be to somewhere else.
       */
    case APPEND_OUT:
      seq++;
      if ( seq->type == WHITESPACE )
	seq++;

      if ( seq->type == NULL_LEX ){
	printf("Syntax error: missing redirect destination.\n");
	return RSH_ERR;
      }

      if ( set_stdout(seq->tok, 1) == RSH_ERR )
	return RSH_ERR;

      break;
      /*
       * Change this commands stderr to be to somewhere else.
       */
    case APPEND_ERR:
      seq++;
      if ( seq->type == WHITESPACE )
	seq++;

      if ( seq->type == NULL_LEX ){
	printf("Syntax error: missing redirect destination.\n");
	return RSH_ERR;
      }

      if ( set_stderr(seq->tok, 1) == RSH_ERR )
	return RSH_ERR;

      break;

      /*
       * Pipe the previous commands output to the next commands input.
       */
    case PIPE:
      /* Some error checking. */
      if ( (seq+1)->type == NULL_LEX ){
	printf("Missing read half of pipe command.\n");
	return RSH_ERR;
      }

      /* First make the pipe. */
      ret = pipe(pipe_fds);
      if ( ret < 0 ){
	perror("pipe");
	return RSH_ERR;
      }

      /* Now we pipe stdout of the current command to where ever... */
      proc_stdout = pipe_fds[1];
 
      /* Set the pipe status. */
      pipe_status |= RSH_PIPE_OUT;
      
      /* Dispatch the process... and get ready for the next. */
      dispatch_process(1);
      clear_argv();

      /* Set up the other half of the pipe for the next process. */
      proc_stdin = pipe_fds[0];
      break;

    case PIPE_ERR:
      /* Some error checking. */
      if ( (seq+1)->type == NULL_LEX ){
	printf("Missing read half of pipe command.\n");
	return RSH_ERR;
      }

      /* First make the pipe. */
      ret = pipe(pipe_fds);
      if ( ret < 0 ){
	perror("pipe");
	return RSH_ERR;
      }

      /* Now we pipe stdout of the current command to where ever... */
      proc_stdout = pipe_fds[1];
      
      /* Set pipe status */
      pipe_status |= RSH_PIPE_ERR;

      /* Dispatch the process, and get ready for the next. */
      dispatch_process(1);
      clear_argv();

      /* Set up the other half of the pipe for the next process. */
      proc_stdin = pipe_fds[0];
      break;

    case BACKGROUND:

      /* 
       * Handle the pipe status. If this is set to out, its a left over from
       * the previous process and should actually be set to RSH_PIPE_IN because
       * we expect to receive input for this processes from the previous
       * process.
       */
      if ( pipe_status & (RSH_PIPE_OUT|RSH_PIPE_ERR) )
	pipe_status = RSH_PIPE_IN;

      dispatch_process(1);
      clear_argv();

      /* And now, we reset the pipe status for the next set of commands. */
      pipe_status = RSH_PIPE_NONE;

      break;

      /*
       * This means the command is over. We should do the final dispatch and
       * capture the results (which we save to $?).
       */
    case NULL_LEX:
      if ( command_state != S_BASE){

	/* Same as the BACKGROUND case. */
	if ( pipe_status & (RSH_PIPE_OUT|RSH_PIPE_ERR) )
	  pipe_status = RSH_PIPE_IN;
	
	ret = dispatch_process(0);
	clear_argv();
	pipe_status = RSH_PIPE_NONE;
	return ret;

      } else {

	return 0;

      }

    }

    seq++;

  }
  
  return RSH_OK;

}

/**
 * Clear the argv, command, argc variables, and reset other fields back to
 * their defaults.
 */
void clear_argv(){

  int i = 0;

  if ( argv ){
    while ( argv[i] != NULL )
      free(argv[i++]);
  }  

  if ( argv )
    free(argv);
  argv = NULL;

  if ( command )
    free(command);
  command = NULL;

  proc_stdin = 0;
  proc_stdout = 1;
  proc_stderr = 2;

  command_state = S_BASE;

}

void append_to_argv(char *arg){

  char **tmp;

  if ( ! arg ) return;

  /* Alloc initial array. */
  if ( ! argv ){
    argv = (char **)malloc(sizeof(char *) * 8 );
    argc = 0;
    max_argc = 8;
  }

  /* Grow argv list if necessary. */
  if ( (argc + 1) >= max_argc ){
    tmp = (char **)malloc( sizeof(char *) * (8 + max_argc) );
    memcpy(tmp, argv, max_argc * sizeof(char *) );
    free(argv);
    argv = tmp;
    max_argc += 8;
  }

  /* Add to argv and null terminate. */
  argv[argc++] = arg;
  argv[argc] = NULL;

}

/*
 * Attempts to set the stdin input to the requested file.
 */
int set_stdin(char *file){

  int fd;

  /* First try to open the file. */
  fd = rsh_open(file, O_RDONLY, 0);
  if ( fd < 0 ){
    perror("rsh_open()");
    return RSH_ERR;
  }

  /* If that succedes, then set stdin to be the new file descriptor. */
  proc_stdin = fd;

  return RSH_OK;

}

/*
 * Attempts to set the stdout output to the specified file.
 */
int set_stdout(char *file, int append){

  int fd;
  int flags = O_CREAT|O_WRONLY;
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  if ( append )
    flags |= O_APPEND;

  fd = rsh_open(file, flags, mode);
  if ( fd < 0 ){
    perror("rsh_open()");
    return RSH_ERR;
  }

  proc_stdout = fd;

  return RSH_OK;

}

/*
 * Attempts to set the stderr output to the specified file.
 */
int set_stderr(char *file, int append){

  int fd;
  int flags = O_CREAT|O_WRONLY;
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  if ( append )
    flags |= O_APPEND;

  fd = rsh_open(file, flags, mode);
  if ( fd < 0 ){
    perror("rsh_open()");
    return RSH_ERR;
  }

  proc_stderr = fd;

  return RSH_OK;

}
