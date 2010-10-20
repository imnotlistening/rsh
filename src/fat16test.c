/*
 * Massage the fat16 code a little bit.
 */

#include <rsh.h>
#include <rshfs.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

extern int builtin_fatinfo(int argc, char **argv, int in, int out, int err);

int main(){

  int err;

  /* Load a file system. See what happens. */
  rsh_init_fs();

  printf("FS core intialized.\n");
  err = rsh_fat16_init("testfs.bin", 4096, 32);
  if ( err ){
    printf("WARNING: Could not load internal FS.\n");
  }
  
  printf("Filesystem has been initialized.\n");
  builtin_fatinfo(0, NULL, 0, 1, 2);
  /*_rsh_fat16_display_fat();*/

  /* Now, we need to start testing out actual file operations. */

  return 0;

}
