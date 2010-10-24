/*
 * Massage the fat16 code a little bit.
 */

#include <rsh.h>
#include <rshfs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

extern int builtin_fatinfo(int argc, char **argv, int in, int out, int err);

extern int rsh_fat16_open(struct rsh_file *file, 
			  const char *pathname, int flags);
extern ssize_t rsh_fat16_write(struct rsh_file *file, 
			       const void *buf, size_t count);
extern ssize_t rsh_fat16_read(struct rsh_file *file, void *buf, size_t count);
extern int rsh_fat16_close(struct rsh_file *file);
extern void _rsh_fat16_display_root_dir();
extern void _rsh_fat16_display_path_dir(const char *path);


int main(){

  int err;

  /* Load a file system. See what happens. */
  rsh_init_fs();

  printf("FS core intialized.\n");
  err = rsh_fat16_init("testfs.bin", 1024*16, 512);
  if ( err ){
    printf("WARNING: Could not load internal FS.\n");
    return 1;
  }
  
  printf("Filesystem has been initialized.\n");
  builtin_fatinfo(0, NULL, 0, 1, 2);
  /*_rsh_fat16_display_fat();*/

  /* Now, we need to start testing out actual file operations. First: path 
   * parsing. */
  char *path = "/home/alex/blah/hello/derr";
  /*
  char *another_path = "/home/alex/c-programs/svn-programs/rsh/trunk/src";
  char *node;
  char *copy = NULL, *next = NULL;

  printf("%s:\n", path);
  while ( (node = _rsh_fat16_parse_path(&copy, &next, path)) != NULL)
    printf(" %s\n", node);
  
  printf("%s:\n", another_path);
  copy = NULL; next = NULL;
  while ( (node = _rsh_fat16_parse_path(&copy, &next, another_path)) != NULL)
    printf(" %s\n", node);
  */

  /* Now we can traverse a path by looking at directory tables and stuff. */
  struct rsh_fat_dirent ent;
  int errcode = _rsh_fat16_path_to_dirent(path, &ent, NULL);
  printf("Error returned: %d\n", errcode);
  if ( errcode < 0 ){
    perror("  _rsh_fat16_path_to_dirent");
  }

  /* Lets try making a directory. */
  printf("MAKING A DIRECTORY\n");
  errcode = rsh_fat16_mkdir("/home");
  if ( errcode ){
    perror("_rsh_fat16_mkdir");
  }
  printf("MAKING MORE DIRECTORYS\n");
  errcode = rsh_fat16_mkdir("/home/alex");
  if ( errcode ){
    perror("_rsh_fat16_mkdir");
  }
  errcode = rsh_fat16_mkdir("/home/jim");
  if ( errcode ){
    perror("_rsh_fat16_mkdir");
  }
  errcode = rsh_fat16_mkdir("/home/bob");
  if ( errcode ){
    perror("_rsh_fat16_mkdir");
  }
  errcode = rsh_fat16_mkdir("/home/alice");
  if ( errcode ){
    perror("_rsh_fat16_mkdir");
  }
  

  _rsh_fat16_display_root_dir();
  printf("ls /home (sortof):\n");
  _rsh_fat16_display_path_dir("/home");

  /* Lets try opening a file. */
  printf("OPENING A FILE\n");
  struct rsh_file f;
  errcode = rsh_fat16_open(&f, "/home/alex/text.txt", O_CREAT|O_TRUNC);
  printf("Error returned: %d\n", errcode);
  if ( errcode < 0 ){
    perror("  rsh_fat16_open");
  }
  printf("Resulting struct rsh_file:\n");
  printf("  offset=%u\n", (uint32_t)f.offset);
  printf("  local=%p\n", f.local);

  char *string = "Hello World";
  int i = 0;
  for ( ; i < 5; i++){
    printf("Writing data to a file:\n");
    printf("  Data: '%s'\n", string);
    ssize_t bytes = rsh_fat16_write(&f, string, 11);
    printf("Wrote %d bytes.\n", (int)bytes);
  }
  char buf[1024];
  for ( i = 0; i < 1024; i++){
    buf[i] = 'a' + (i%26);
  }
  printf("Writing data to a file:\n");
  ssize_t bytes = rsh_fat16_write(&f, buf, 1024);
  printf("Wrote %d bytes.\n", (int)bytes);

  for( i = 0; i < 10; i++)
    buf[i] = 0;
  rsh_fat16_write(&f, buf, 10);
  
  _rsh_fat16_display_path_dir("/home/alex/");
  
  /* Read the file back. */
  char inc[76];

  f.offset = 0;
  memset(inc, 0, 76);
  while ( (bytes = rsh_fat16_read(&f, inc, 75)) != 0 ){
    if ( bytes < 0 ){
      perror("rsh_fat16_read");
      return 1;
    }
    printf("%s\n", inc);
    memset(inc, 0, 76);
  }

  /* And close the file. */
  rsh_fat16_close(&f);

  return 0;

}
