/*
 * Structs, defines, functions, etc for dealing with builtin commands.
 */

#ifndef _BUILTIN_H
#define _BUILTIN_H

#include <rshio.h>

/* A typedef for builtin functions. */
typedef int (* builtin_func)(int argc, char **argv, int in, int out, int err);

/* The basic struct for each built in command. */
struct builtin {

  char *name;
  builtin_func func;

};

/* Some ease of use functions. */
struct builtin *rsh_identify_builtin(char *command);

/* Use this macro to close all file descriptors passed to a built in. */
#define RSH_BUILTIN_CLOSE(fd)			\
  do {						\
    if ( fd > 2 )				\
      close(fd);				\
  } while (0)


#endif
