/*
 * This is RSH's implementation of I/O wrappers for the C std library.
 */

#include <rsh.h>
#include <rshio.h>
#include <rshfs.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>


#define BUFFER_SIZE 512

/*
 * If set, the shell will read from the native file system. If this is not set,
 * RSH IO wrappers will read from the built in file system.
 */
int native = 0;

/*
 * printf() functionality for for a file descriptor.
 */
int rsh_dprintf(int fd, char *format, ...){

  va_list args;
  char buf[BUFFER_SIZE];
  int len;
  int written;

  va_start(args, format);

  vsnprintf(buf, BUFFER_SIZE, format, args);
  len = strlen(buf);

  written = rsh_write(fd, buf, len);
  if ( written != len )
    return RSH_ERR;

  return RSH_OK;

}

/*
 * Specify if RSH should read of off the native file system or the built in
 * file system.
 */
inline void rsh_fs(int where){

  native = where;
  
}

/*
 * Return > 0 if the specified path is located on the native file system. If
 * the path is a relative path, then the path will be interpretted as native if
 * RSH is currently using the naitve file system as default or visa versa.
 */
int rsh_path_location(char *path){

  return 0;

}

/*
 * Set access to the native or built in file system.
 */
int builtin_native(int argc, char **argv, int in, int out, int err){

  if ( native ){
    native = 0;
    rsh_dprintf(out, "Using built in filesystem.\n");
  } else {
    native = 1;
    rsh_dprintf(out, "Using native filesystem.\n");
  }

  return 0;

}

/*
 * Wrapper for read(). This will either read from the native FS or the built in
 * FS depending on the the value of 'native'.
 */
ssize_t rsh_read(int fd, void *buf, size_t count){

  if ( ! _RSH_FD(fd) )
    return read(fd, buf, count);
  else
    return _rsh_read(fd, buf, count);

}

/*
 * Wrapper for write(). Like rsh_read().
 */
ssize_t rsh_write(int fd, const void *buf, size_t count){

  if ( ! _RSH_FD(fd) )
    return write(fd, buf, count);
  else
    return _rsh_write(fd, buf, count);
  
}

/*
 * Not yet implemented.
 */
int rsh_dup2(int oldfd, int newfd){

  return -1;

}

/*
 * Wrapper for open().
 */
int rsh_open(const char *pathname, int flags, mode_t mode){

  if ( native )
    return open(pathname, flags, mode);
  else
    return _rsh_open(pathname, flags, mode);

}

/*
 * Wrapper for close().
 */
int rsh_close(int fd){

  if ( ! _RSH_FD(fd) )
    return close(fd);
  else
    return _rsh_close(fd);

}

/*
 * Wrapper for readdir(). This will *only* work for builtin functions since
 * the libc version uses a pointer to a DIR type in order to specify the file.
 * Here we use a file descriptor instead.
 */
struct dirent *rsh_readdir(int dfd){

  if ( ! _RSH_FD(dfd) )
    return NULL;
  else
    return _rsh_readdir(dfd);

}
