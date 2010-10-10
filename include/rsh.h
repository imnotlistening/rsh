/**
 * Common defines for the RIT shell.
 */

#ifndef _RSH_H
# define _RSH_H

#include <parser.h>

/*
 * Definitions.
 */
#define RSH_OK    0
#define RSH_ERR   -1

/*
 * Read term stuff. Ugh. This struct keeps track of a buffer. This will make
 * the up arrow/down arrow command completion easier (I hope).
 */
struct rsh_buff {

  /* The underlying buffer itself. */
  char *buf;

  /* The offset to write the next character to. */
  int offset; 

  /* The number of characters in the buffer. */
  int len;
  
  /* The size of the buffer. */
  int size;

  /* Does this buffer have stuff? */
  int used;

};

/*
 * History stack frame type.
 */
struct rsh_history_frame {

  /* Offset into the history table. */
  int text;
  
  /* Stack is implemented as a linked list. */
  struct rsh_history_frame *next;

};

/*
 * History stack type. Hehe.
 */
struct rsh_history_stack {

  /* Top of the stack. */
  struct rsh_history_frame *top;

};

struct file_descriptors {

  /* List of file descriptors. */
  int *fds;
  
  /* Length of fd array. */
  int length;

  /* Index of next place to insert a fd. */
  int offset;

};

/*
 * These are useful functions. Very informative, I know.
 */
int  interactive_shell();
int  script_shell();
int  rsh_parse_args(int argc, char *argv[]);
void rsh_init();
void rsh_rc_init();

int  run_script();
int  run_interactive();
int  rsh_command(struct rsh_token *seq);

void append_to_argv(char *arg);
void clear_argv();

void rsh_history_print();
void rsh_history_display_async(int sig);
void rsh_history_add(char *line);
void rsh_historify(char *line);
void rsh_history_init();
void rsh_term_init();
int  rsh_buf_insert(struct rsh_buff *buf, char c);
void rsh_buf_backspace(struct rsh_buff *buf);
void rsh_buf_delete(struct rsh_buff *buf);
int  rsh_buf_append(struct rsh_buff *buf, char c);
int  rsh_buf_copy(struct rsh_buff *dest, struct rsh_buff *src);
void rsh_buf_clean(struct rsh_buff *buf);
void rsh_buf_shift(struct rsh_buff *buf, int direction);
void rsh_buf_display(struct rsh_buff *buf);
void rsh_stack_init(struct rsh_history_stack *stack);
void rsh_stack_clean(struct rsh_history_stack *stack);
int  rsh_stack_push(struct rsh_history_stack *stack, int text);
int  rsh_stack_pop(struct rsh_history_stack *stack);

void prompt_init();
void prompt_print();

void rsh_init_fds();
int  rsh_register_fd(int fd);
void rsh_close_fds();

/* Macros. */
#define STACK_ITER(elem) (elem = elem->next)

/*
 * These are defines that control the behavior of RSH.
 */

/* Specify the history size. */
#define HIST_SIZE 20

/* Chunk size of terminal string buffers. */
#define BUF_CHUNK 128

#endif
