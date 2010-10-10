/*
 * Test out the history circular buffer. Its kinda eh.
 */

#include <stdio.h>
#include <unistd.h>
#include <termios.h>

#include <rsh.h>
#include <lexxer.h>

extern int interactive;

int main(){

  /* Init history buffer. */
  rsh_history_init();
  
  /* Add some data. */
  rsh_history_add("1");
  rsh_history_add("2");
  rsh_history_add("3");
  rsh_history_add("4");
  rsh_history_add("5");
  rsh_history_add("6");
  rsh_history_add("7");
  rsh_history_add("8");
  rsh_history_add("9");
  rsh_history_add("10");
  rsh_history_add("11");
  rsh_history_add("12");
  rsh_history_add("13");
  rsh_history_add("14");
  rsh_history_add("15");
  rsh_history_add("16");
  rsh_history_add("17");
  rsh_history_add("18");
  rsh_history_add("19");
  rsh_history_add("20");
  rsh_history_add("21");
  rsh_history_add("22");
  rsh_history_add("23");
  rsh_history_add("HELLO WORLD");
  rsh_history_print();

  /*
  int i = 0;
  for ( ; i < 21; i++){
    rsh_history_add("12345--\n");
  }
  rsh_history_print();
  */

  /* Now start testing the rsh-readline code. */
  char buf[49];
  int bytes = 0;
  
  rsh_set_input(0);
  interactive = 1;

  struct termios settings;
  tcgetattr(0, &settings);
  settings.c_cc[VMIN] = 1;
  settings.c_cc[VTIME] = 0;
  settings.c_lflag &= ~ECHO;
  settings.c_lflag &= ~ICANON; 
  tcsetattr(0, TCSANOW, &settings);
  fflush(stdout);
  
  while ( 1 ){
    printf("--> ");
    fflush(stdout);
    bytes = rsh_readbuf(buf, 48);
    if ( ! bytes )
      break;
    buf[bytes] = 0;
    
    if ( buf[bytes-1] == '\n' )
      buf[bytes-1] = 0;

    printf("(%d) '%s'\n", bytes, buf);
    if ( *buf )
      rsh_history_add(buf);
  }

  return 0;

}
