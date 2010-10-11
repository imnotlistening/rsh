/*
 * The goal of this code is to interface with the flex YY_INPUT macro. The
 * macro is passed three arguments, a buffer, an interger variable to store the
 * chars read, and finally, the maximum space available in the buffer. Our
 * goal is to write a function that can either get input from a file or from
 * a terminal without flex knowing the difference. The code that reads from the
 * terminal can also do clever things like tab completion, command line history
 * and whatever else gnu-term-readline does.
 *
 * This code supports either gnu-term-readline or my own much dumber readline
 * equivalent. Sadly, a section implemented with the read() syscall must exist
 * as per project requirements. Oh well. Otherwise the interactive data reading
 * would all just be through gnu-term-readline and require significantly less 
 * code. Due to this requirement, I also had to reimplement the lexxer's input
 * function macro YY_INPUT(). Look at the parser.lex file for that defintion.
 * Basically it just calls rsh_readbuf(). rsh_readbuf() handles interactive
 * vs file I/O, gnu-term-readline vs rsh-readline calls, etc. When using gnu-
 * term-readline() history is implemented by the gnu-term-readline code. That
 * is *inifitetly* simpler than mine. However, my implementation of a circular
 * list is pretty nifty IMHO.
 */

#include <rsh.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>

/* 
 * The history buffer's actual size is 1 larger than the desired history size.
 */
#define _HIST_BUFFER_SIZE (HIST_SIZE+1)

/*
 * This is what flex uses to hold the number of characters read by YY_INPUT.
 */
extern int yy_n_chars;

/*
 * Is the RSH shell interactive or not? This determines which _rsh_read_*()
 * function gets called.
 */
extern int interactive;

/* Read function prototypes. */
size_t _rsh_read_term(char *buffer, size_t max);
size_t _rsh_read_file(char *buffer, size_t max);
char  *_rsh_do_read_line();

/* The file descriptor to read from. Set by rsh_set_input(). */
int rsh_fd = 0;

/* Read term history circular buffer. history_max points to the last available
 * slot in memory. start points to the actual start of the buffer, end the end
 * of the buffer. The 'end' can be lower in memory than the 'start' if we have
 * wrapped around. current points to our current history line. */
char *history[_HIST_BUFFER_SIZE];
char **history_max = history + _HIST_BUFFER_SIZE;
int start;
int end;
int current;

/* This is for keeping track of the history being displayed on the terminal. */
struct rsh_history_stack hist_stack = { NULL };

/* This is the buffer of characters being read for interactive mode.  We 
 * declare it here so that the signal handler for history completion can access
 * it and restore the terminal after it has printed the history. Also declare a
 * a buffer to back the read characters up in while we handle up arrow/down
 * arrow keys.
 */
struct rsh_buff buf;
struct rsh_buff backup;

/*
 * Macros to edit the terminal line.
 */
#define CUR_SAVE() printf("\033[s")
#define CUR_RESTORE() printf("\033[u")
#define CUR_KILL() printf("\033[K")
#define CUR_SHIFT_RIGHT(offset) printf("\033[%dC", offset);
#define CUR_SHIFT_LEFT(offset) printf("\033[%dD", offset);
#define CUR_NEXT_LINE() printf("\033[E");

#define IGN_NEXT_CHAR() read(rsh_fd, &c, 1)

/*
 * Macros for incrementing and decrementing a pointer into the history. ptr is
 * not actually a pointer anymore, its an int that points to an offset into the
 * history list.
 */
#define INCREMENT(ptr)				\
  do {						\
    ptr++;					\
    if (ptr >= _HIST_BUFFER_SIZE)		\
      ptr = 0;					\
  } while (0)
#define DECREMENT(ptr)				\
  do {						\
    ptr--;					\
    if (ptr < 0)				\
      ptr = _HIST_BUFFER_SIZE - 1;		\
  } while (0)


/*
 * Initialize the history buffer.
 */
void rsh_history_init(){

  memset(history, 0, sizeof(char *) * _HIST_BUFFER_SIZE);
  start = 0;
  end = 0;

}

/*
 * Initialize the terminal.
 */
void rsh_term_init(){

  struct termios settings;
  tcgetattr(rsh_fd, &settings);
  settings.c_cc[VMIN] = 1;
  settings.c_cc[VTIME] = 0;
  settings.c_lflag &= ~ECHO;
  settings.c_lflag &= ~ICANON; 
  tcsetattr(rsh_fd, TCSANOW, &settings);

}

/*
 * Read some characters. Return the number of characters stored into buffer.
 * Read at most 'size' characters. See rsh_set_input() for how this function
 * determines where to read input from.
 */
size_t rsh_readbuf(char *buffer, size_t max){

  if ( interactive )
    /* Read from the terminal with lots of cool features. */
    return _rsh_read_term(buffer, max);
  else
    return _rsh_read_file(buffer, max);

}

/*
 * Set the input file descriptor. This can be anything really...
 */
void rsh_set_input(int fd){

  rsh_fd = fd;

}

/*
 * Use the GNU readline functionality.
 */
#ifdef _HAVE_GNU_READLINE
size_t _rsh_read_term_gnu(char *buf, size_t max){

  return 0;

}
#endif

/*
 * Use my hackjob of a readline library. This function will return the '\n'
 * that ends each line of input typed by the user. This is so that the lexxer
 * doesn't cry. The reason this function is required is because we must
 * translate between 'readline' and 'read buff', which is what the lexxer does.
 * As such this code reads a line from the terminal, saves it, and uses the 
 * saved line for repeated calls (up until the line runs out). The new line
 * signifying the end of the line is also returned (unlike the libc functions)
 * to make the parser/lexxer happy.
 */
size_t _rsh_read_term_rsh(char *buf, size_t max){
  
  /* We could read more data from the terminal than 'max' bytes. If we do have
   * extra, we should return that data instead of a whole new call to 
   * _rsh_do_read_line().
   */
  static char *line = NULL;
  static char *line_ptr = NULL;
  static int historified = 0;
  int bytes = 0;

  if ( ! line ) {
    line = _rsh_do_read_line();
    
    /*
     * This is the end of input, like Control-D or something.
     */
    if ( line == NULL)
      return 0;
    
    line_ptr = line;
  }

  /*
   * Add this line to the history if we haven't already tried to add it.
   */
  if ( ! historified ){
    rsh_historify(line);
    historified++;
  }

  /*
   * Copy the buffer returned from _rsh_do_read_line() into buf.
   */
  while ( *line_ptr != 0 && (line_ptr - line) < max )
    buf[bytes++] = *(line_ptr++);

  /*
   * Are we done? line is cleaned up else where in the code, we should not call
   * free() here or nasty things happen.
   */
  if ( ! *line_ptr ){
    line = NULL;
    line_ptr = NULL;
    historified = 0;
  } else {
    line += bytes;
  }

  return bytes;

}

size_t _rsh_read_term(char *buf, size_t max){

  /*
   * Check if we are building for gnu readline or if we have to use the crappy
   * readline stuff that I wrote.
   */
#ifdef _HAVE_GNU_READLINE
  return _rsh_read_term_gnu(buf, max);
#else
  return _rsh_read_term_rsh(buf, max);
#endif

  return 0;

}

/*
 * Read data from a script file.
 */
size_t _rsh_read_file(char *buf, size_t max){

  /* 
   * This is called by a non-interactive shell. Just do a read() call and
   * attempt to return sensable data.
   */
  return read(rsh_fd, buf, max);

}

/*
 * Add a line to the history buffer.
 */
void rsh_history_add(char *line){

  /* In case we are replacing an old line. */
  if ( history[end] )
    free(history[end]);

  history[end] = strdup(line);

  /* Perform book keeping... Handle wrapping, deleting old entries, etc. */
  INCREMENT(end);

  if ( end == start )
    INCREMENT(start);

}

/*
 * Take the passed line and figure out of it should be added to the history.
 * Basically make sure there is more than just wihite space in the line.
 */
void rsh_historify(char *line){

  int ok = 0;
  char *copy;
  int del;

  /* Don't even bother. */
  if ( strlen(line) < 1 )
    return;

  while ( *line ){
    if ( *line != ' ' || *line != '\t' || *line != '\n' ){
      ok = 1;
      break;
    }
  }
  
  if ( ! ok )
    return;

  /* Make a copy of the string that we can edit. */
  copy = strdup(line);
  if ( ! copy )
    return;

  /* Turn all \n characters we find into nulls. This is ok since there should
   * only ever be 1 \n per string passed and it should be the last character. 
   */
  del = strlen(copy) - 1;
  copy[del] = 0;

  if ( strlen(copy) > 0 )
    rsh_history_add(copy);
  
  free(copy);

}

void rsh_history_print(){

  int offset = start;
  int printed = 1;

  /* Start is the oldest entry, so start printing there. */
  while ( offset != end ){
    printf("%-3d %s\n", printed++, history[offset]);
    INCREMENT(offset);
  }

}

/*
 * Fill buf with a line from the history. 0 = up, non-zero = down.
 */
void _rsh_do_history_completion(int direction){

  int tmp;
  char *line;

  if ( start == end ) /* Empty history. */
    return;

  /* 
   * The first thing we must do is backup the current characters in the
   * buffer. Users don't want their current command to disappear. However, we
   * should only do this if the backup is not already filled. If the back up
   * is filled, we can assume that we have already backed up the terminal at
   * some point.
   */
  if ( ! backup.used )
    rsh_buf_copy(&backup, &buf);

  /* Clean the buf so we can put the history line into buf. */
  rsh_buf_clean(&buf);
  
  /* This is up, as in we go back into previous commands. */
  if ( direction == 0 ){
    
    /* Handle the wrap. */
    if ( current == end || ! history[current] ){
      current = end;
      DECREMENT(current);
      rsh_stack_clean(&hist_stack);
    }
    
    tmp = current;
    DECREMENT(current);
    rsh_stack_push(&hist_stack, tmp);
      
    /* Copy the text into buf. */
    line = history[tmp];

    while ( *line )
      rsh_buf_insert(&buf, *line++);

  } else {

    /* Pop the top frame of the stack, then use the new top frame. */
    rsh_stack_pop(&hist_stack);

    /* This signifies that we should restore the line being edited. */
    if ( ! hist_stack.top ){
      rsh_buf_copy(&buf, &backup);
      current = end;
      goto out_rewrite_line;
    }

    /* On the other hand, if we actually have a stack top, copy it into buf */
    INCREMENT(current);
    line = history[hist_stack.top->text];

    while ( *line )
      rsh_buf_insert(&buf, *line++);

  }

 out_rewrite_line:
  rsh_buf_display(&buf);
  return;

}

/*
 * When a user hits the left/right arrow key we should change the cursor
 * location. Derr.
 */
void _rsh_do_move_cursor(int direction){

  rsh_buf_shift(&buf, direction);
  rsh_buf_display(&buf);

}

/*
 * Performs a backspace from the current cursor location.
 */
void _rsh_do_backspace(){

  rsh_buf_backspace(&buf);
  rsh_buf_display(&buf);

}

/*
 * Do a delete.
 */
void _rsh_do_delete(){

  rsh_buf_delete(&buf);
  rsh_buf_display(&buf);

}

/*
 * Called by _rsh_do_read_line() to handle escape sequences (primarily this is
 * how the up arrow key and tab and the like are handled.
 */
int _rsh_handle_escape_seq(){

  char c;
  int ret;

  /* Assume we have already read the escape character itself. Thus we are only
   * interested in the next few characters... */
  ret = read(rsh_fd, &c, 1);
  if ( ! ret ){
    perror("read()");
    return RSH_ERR;
  }

  /* We have the first escape char, what is it? */
  switch ( c ){

    /*
     * OK, now we should see what the next char is.
     */
  case '[':
    ret = read(rsh_fd, &c, 1);
    if ( ! ret ){
      perror("read()");
      return RSH_ERR;
    }
    switch ( c ){
      
      /*
       * Up arrow and down arrow. This is history cycling.
       */
    case 'A': /* Up */
    case 'B': /* Down */
      _rsh_do_history_completion(c - 'A');
      return RSH_OK;

    case 'C': /* Right */
    case 'D': /* Left */
      _rsh_do_move_cursor(c - 'C');
      return RSH_OK;

      /*
       * Insert, ignore the tilda...
       */
    case '2':
      IGN_NEXT_CHAR();
      break;

    case '3':
      _rsh_do_delete();
      IGN_NEXT_CHAR();
      break;

    }

    break;

  default:
    printf("<0x%02x>", c);

  }

  return RSH_ERR;

}

/*
 * Handle the gory details of adding a character to a variabel sized buffer.
 */
int rsh_buf_insert(struct rsh_buff *buf, char c){

  char d, *tmp;
  int offset;

  /* Handle the empty buffer. */
  if ( ! buf->buf ){
    buf->buf = (char *)malloc(sizeof(char) * BUF_CHUNK);
    if ( ! buf->buf )
      return RSH_ERR;
    buf->size = BUF_CHUNK;
    buf->offset = 0;
    buf->len = 0;
  }

  /* Grow our buffer if necessary. */
  if ( buf->len >= buf->size ){
    tmp = (char *)malloc(buf->size + BUF_CHUNK);

    /* Handle memory errors. */
    if ( ! tmp ){
      return RSH_ERR;
    }
    free(buf->buf);
    buf->size += BUF_CHUNK;
    buf->buf = tmp;

  }

  /* Finally, time to write the character. */
  offset = buf->offset++; 

  /*
   * Write the character to be inserted into the buffer, then copy the saved 
   * char (d) to the just inserted char. Keep doing this until we have shifted
   * each character down 1 spot.
   */
  while ( offset <= buf->len ){
    d = buf->buf[offset];   /* Save the char that the offset points to. */
    buf->buf[offset++] = c;
    c = d;
  }
  buf->len++;
  buf->used = 1;

  return RSH_OK;

}

/*
 * Shift the position at which characters will be inserted. This corresponds
 * to left/right arrow keys. Right = 0, Left = non-zero.
 */
void rsh_buf_shift(struct rsh_buff *buf, int direction){

  /* Move left. */
  if ( direction ){

    /* We are already fully left justified. */
    if ( buf->offset <= 0 ) 
      return;

    /* Otherwise, just decrement buf->offset. */
    buf->offset--;

  } else {
    
    /* Fully right justified. */
    if ( buf->offset >= buf->len ) 
      return;

    /* Add one to the offset. */
    buf->offset++;

  }

}

/*
 * Perform a 'backspace'. This is essentially a shift left, then delete.
 */
void rsh_buf_backspace(struct rsh_buff *buf){

  rsh_buf_shift(buf, 1);
  rsh_buf_delete(buf);

}

/*
 * Perform a delete: remove the character that the cursor is hovering over and
 * then shift the remaining characters 1 to the left.
 */
void rsh_buf_delete(struct rsh_buff *buf){

  int offset = buf->offset;

  /*
   * This means the cursor is hovering over an empty slot at the farmost right
   * of the buffer, i.e: delete is meaningless.
   */
  if ( buf->offset == buf->len )
    return;

  /*
   * Ok, here we just shift each character to the right of the cursor, 1 to the
   * left.
   */
  while ( offset < buf->len ){
    buf->buf[offset] = buf->buf[offset+1];
    offset++;
  }

  if ( buf->len > 0 )
    buf->len--;

}

/*
 * Perform a deep copy of a struct rsh_buff. Make sure you copy into a clean
 * or uninitialized struct_rsh_buff otherwise this will generate a memory leak.
 */
int rsh_buf_copy(struct rsh_buff *dest, struct rsh_buff *src){

  dest->size = src->size;
  dest->offset = src->offset;
  dest->len = src->len;
  dest->used = src->used;
  if (src->buf){
    /* Make a new buf, and copy source's contents to that buf. */
    dest->buf = (char *)malloc(dest->size * sizeof(char));
    if ( ! dest->buf )
      return RSH_ERR;

    memcpy(dest->buf, src->buf, dest->offset);

  } else {
    dest->buf = NULL;
  }

  return RSH_OK;

}

/*
 * Clean up after a buffer. The buffer can now be reused with rsh_buf_add() if
 * so desired.
 */
void rsh_buf_clean(struct rsh_buff *buf){

  /* Simple stuff. */
  if ( buf->buf ){
    //printf("free buffer: %p\n", buf->buf);
    free(buf->buf);
  }

  buf->buf = NULL;
  buf->size = 0;
  buf->offset = 0;
  buf->used = 0;
  buf->len = 0;

}

/*
 * Write a terminating null into a buffer.
 */
int rsh_buf_append(struct rsh_buff *buf, char c){

  int offset, ret;
  
  /* Save the buffer location. */
  offset = buf->offset;

  /* Make sure the insert location is at the end of the buffer. */
  buf->offset = buf->len;

  /* Then just insert a null, restore the original offset, and return. */
  ret = rsh_buf_insert(buf, c);
  buf->offset = offset;
  return ret;

}

/*
 * Display a buffer to the terminal. We assume that end of the prompt was saved
 * by the _rsh_do_read_line() function, thus we restore the cursor position,
 * kill the line, and display the buffer. Then flush stdout.
 */
void rsh_buf_display(struct rsh_buff *buf){

  int i = 0;

  /* Wipe the current text. */
  CUR_RESTORE();
  CUR_KILL();

  /* Rewrite the text */
  while ( i < buf->len )
    putchar(buf->buf[i++]);

  /* Set the blinking cursor offset. */
  CUR_RESTORE();
  /* Shifting zero elems to the right doesn't seem to work... */
  CUR_SHIFT_LEFT(1);
  CUR_SHIFT_RIGHT(buf->offset + 1);

  fflush(stdout);

}

/*
 * RSH stack data type for dealing with the up/down arrow keys. Up/Down cycling
 * is either way more complex than I thought it was or I am seriously over
 * thinking this :(.
 */
void rsh_stack_init(struct rsh_history_stack *stack){

  stack->top = NULL;

}

/*
 * Clean a stack up.
 */
void rsh_stack_clean(struct rsh_history_stack *stack){

  struct rsh_history_frame *frame;
  struct rsh_history_frame *tmp;

  if ( ! stack->top )
    return;

  frame = stack->top;
  while ( frame != NULL){

    tmp = frame;
    STACK_ITER(frame);
    free(tmp);

  }

  stack->top = NULL;
  return;

}

/*
 * Push an elem onto the stack. Return RSH_ERR on error.
 */
int rsh_stack_push(struct rsh_history_stack *stack, int text){

  struct rsh_history_frame *frame = 
    (struct rsh_history_frame *)malloc(sizeof(struct rsh_history_frame));
  if ( ! frame )
    return RSH_ERR;

  frame->text = text;
  frame->next = stack->top;

  stack->top = frame;

  return RSH_OK;

}

/*
 * Pop an elem off the stack and deallocate its memory. Returns the integer
 * offset into the history list of the popped frame. Returns RSH_ERR if there
 * is an error (empty stack).
 */
int rsh_stack_pop(struct rsh_history_stack *stack){

  int frame_data;
  struct rsh_history_frame *frame;
  
  if ( ! stack->top )
    return RSH_ERR;

  frame = stack->top;
  frame_data = stack->top->text;
  STACK_ITER(stack->top);
  free(frame);
  return frame_data;

}

/*
 * This is the core routine for my read term code. Here is where things like
 * the up arrow key, editing keys, etc are dealt with.
 */
char *_rsh_do_read_line(){

  char c;

  rsh_buf_clean(&buf);
  buf.used = 1; /* For empty command lines. */
  current = end;
  DECREMENT(current);
  rsh_stack_clean(&hist_stack);

  CUR_SAVE();

  /* Now that we have a buffer... Start filling it. */
  while ( read(rsh_fd, &c, 1) != 0 ){
    
    /* Handle escape sequences. 0x1b -> hex escape character. */
    if ( c == 0x1b ){
      _rsh_handle_escape_seq();
      
      /* Regardless, we do not want to put the escape character into the buf */
      continue;
    }

    /* Reset the backup buffer so up/down arrow works the way we expect.  */
    rsh_buf_clean(&backup);
  
    /* Deal with EOT from the terminal (Cntr-D) */
    if ( c == 0x04 )
      break;

    /* Backspace */
    if ( c == 0x7f ){
      _rsh_do_backspace();
      goto _display;
    }

    /* 
     * Deal with the newline.  Otherwise, just add the character to the buffer.
     */
    if ( c == '\n' )
      rsh_buf_append(&buf, c);
    else
      rsh_buf_insert(&buf, c);

    /* Now redisplay the buffer. */
  _display:
    rsh_buf_display(&buf);

    /* If we hit a NL then we should return. */
    if ( c == '\n')
      break;

  }

  /* Make sure the buffer is NULL terminated. */
  rsh_buf_append(&buf, 0);

  /* Now set the terminal cursor to where its exepcted to be. */
  CUR_NEXT_LINE();
  fflush(stdout);

  return buf.buf;

}

/*
 * Display the history asynchronously.
 */
void rsh_history_display_async(int sig){

  /* First tings first, print a NL. */
  printf("\n");
  
  /* Now display the history. */
  rsh_history_print();

  /* Now we have to restore the terminal. */
  prompt_print();
  CUR_SAVE();
  rsh_buf_display(&buf);

}

/*
 * Reset the prompt. This is useful for when back grounded processes die.
 */
void rsh_reset_input(){

  /* Now we have to restore the terminal. */
  prompt_print();
  CUR_SAVE();
  rsh_buf_display(&buf);

}
