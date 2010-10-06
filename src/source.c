/*
 * Source a script. That is to say, run a script in this interactive shell.
 * This is how things like ~/.rshrc get parsed. This code plug into the shell
 * via the builtin interface. See builtin.c for how to do this. Mostly you
 * just need to implement a particular function and make sure a pointer to it
 * is in the right array. Anyway, go look at builtin.c.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <rsh.h>
#include <exec.h>
#include <lexxer.h>
#include <builtin.h>
#include <symbol_table.h>

/* 
 * We need these to save our state and restore our state after the source
 * command finishes. */
extern char *script;
extern char **script_argv;
extern int interactive;
extern int EOF_encountered;

/*
 * We need a really lightweight and temporary stack to hold all the numeric
 * symbol table values. I probably should have implemented a more generic stack
 * data structure else where, but oh well.
 */
struct _sym_stack_frame {

  char *name;
  char *data;
  struct _sym_stack_frame *next;

};
typedef struct _sym_stack_frame sym_stack_head;

void _sym_stack_push(sym_stack_head *stack, char *name, char *val);
void _sym_stack_del_frame(struct _sym_stack_frame *frame);
struct _sym_stack_frame *_sym_stack_pop(sym_stack_head *stack);

#define _SYM_STACK_ITERATE(stack, frame)	\
  for ( frame = stack->next; frame != NULL; frame = frame->next)
#define _SYM_STACK_INIT(stack)			\
  stack.name = 0;				\
  stack.data = 0;	 		        \
  stack.next = 0;

/*
 * Source a script, that is to say execute the script non-interactively in the
 * shell's environment. This is primarily useful for startup scripts: ~/.rshrc
 */
int builtin_source(int argc, char **argv, int in, int out, int err){

  int ret;
  int script_file;
  int interactive_save;
  int EOF_encountered_save;
  char *script_save;
  char *name, *val;
  char **script_argv_save;
  struct sym_entry *ent = NULL;
  sym_stack_head syms;
  struct _sym_stack_frame *tmp;

  /* Ignore 'source' in argv. */
  argv++;
  argc--;

  /* 
   * Save the shell's state (for when we have a source command in a sourced 
   * script or shell script) State include: args, numeric symbols in the symbol
   * table, etc.
   */
  interactive_save = interactive;
  EOF_encountered_save = EOF_encountered;

  script_save = script;
  script_argv_save = script_argv;

  interactive = 0;     /* We are a script, not interactive any more. */
  EOF_encountered = 0; /* New file, EOF probably hasn't been hit yet. */
  script = *argv;      /* Our new script. */
  script_argv = (argv+1);  /* Our new arguements. */

  /* Here is the symbol table save, only the numeric ones though. */
  _SYM_STACK_INIT(syms);
  while ( symtable_numeric(&ent, &name, &val) )
    _sym_stack_push(&syms, name, val);

  /* Now, we delete everything we saved. */
  _SYM_STACK_ITERATE((&syms), tmp)
    symtable_remove(tmp->name);
  
  /* Open the script file and stuff. */
  script_file = open(script, O_RDONLY);
  if ( script_file < 0 ){
    perror(script);
    ret = RSH_ERR;
    goto clean_up;
  }

  /* Now we should be ready to source the script. */
  rsh_set_input(script_file);
  ret = run_script();

 clean_up:
  /* And now we have to restore evrything... */
  interactive = interactive_save;
  EOF_encountered = EOF_encountered_save;

  script = script_save;
  script_argv = script_argv_save;

  rsh_set_input(0); /* Input should be stdin. */

  return ret;

}

void _sym_stack_push(sym_stack_head *stack, char *name, char *data){

  struct _sym_stack_frame *prev = NULL;
  struct _sym_stack_frame *frame = NULL;
  struct _sym_stack_frame *new_frame = NULL;
  
  _SYM_STACK_ITERATE(stack, frame){
    prev = frame;
  }

  /* Allocate some storage. */
  new_frame = (struct _sym_stack_frame *)
    malloc(sizeof(struct _sym_stack_frame));

  /* We fail silently. Cause I feel like it. */
  if ( ! new_frame )
    return;

  if ( data )
    new_frame->data = strdup(data);
  else
    new_frame->data = NULL;

  new_frame->name = strdup(name);
  new_frame->next = NULL;

  /* Stack is empty. */
  if ( ! prev ){
    stack->next = new_frame;
    return;
  }

  prev->next = new_frame;

}

struct _sym_stack_frame *_sym_stack_pop(sym_stack_head *stack){

  struct _sym_stack_frame *prev = NULL;
  struct _sym_stack_frame *frame = NULL;
  
  _SYM_STACK_ITERATE(stack, frame){
    prev = frame;
    if ( frame->next == NULL )
      break;
  }

  prev->next = NULL;
  return frame;

}

void _sym_stack_del_frame(struct _sym_stack_frame *frame){

  free(frame->data);
  free(frame->name);
  free(frame);
  
}
