/*
 * Structs, defines, functions, etc for dealing with builtin commands.
 */

#ifndef _BUILTIN_H
#define _BUILTIN_H

/* A typedef for builtin functions. */
typedef int (* builtin_func)(int argc, char **argv, int in, int out, int err);

/* The basic struct for each built in command. */
struct builtin {

  char *name;
  builtin_func func;

};

/* Some ease of use functions. */
struct builtin *rsh_identify_builtin(char *command);

#endif
