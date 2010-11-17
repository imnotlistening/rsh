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
 * The name of the filesystem. Defined in fs.c.
 */
extern char *rsh_root;

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
 * native_path: returns true if the string passed is a path on the native file
 * system, false otherwise. This will not check to see if teh path exists, it
 * simply looks at the format of the passed string and makes a determination.
 * The way this works is as follows: if the path is not absolute, then the
 * value of 'native' is returned, that is a relative path is going to exist on
 * whatever file system is currently being used. If the path is absolute then
 * the path is native, unless the path starts with /<fs_name>, in which case
 * it is built in and must be translated to a absolute builtin path with the
 * function rsh_abs_bifs_path().
 */
int rsh_native_path(const char *path){

  if ( *path != '/' )
    return native;

  if ( strncmp(path, rsh_root, strlen(rsh_root)) )
    return 1;
  return 0;

}

/*
 * Returns the subsection of the passed string that is absolute relative to the
 * root of the built in file system. 
 */
char *rsh_builtin_path(char *path){

  return path + strlen(rsh_root);

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

  if ( rsh_native_path(pathname) ){
    return open(pathname, flags, mode);
  } else {
    return _rsh_open(pathname, flags, mode);
  }

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

/*
 * Wrapper for stat().
 */
int rsh_fstat(int fd, struct stat *buf){

  if ( ! _RSH_FD(fd) )
    return fstat(fd, buf);
  else
    return _rsh_fstat(fd, buf);

}

/*
 * Wrapper for mkdir().
 */ 
int rsh_mkdir(const char *path, mode_t mode){

  if ( native )
    return mkdir(path, mode);
  else
    return _rsh_mkdir(path);

}

/*
 * Wrapper for unlink().
 */ 
int rsh_unlink(const char *path){

  if ( native )
    return unlink(path);
  else
    return _rsh_unlink(path);

}

/*
 * Wrapper for chdir().
 */
int rsh_chdir(const char *dir){

  if ( native )
    return chdir(dir);
  else
    return _rsh_chdir(dir);

}

/*
 * Wrapper for getcwd().
 */
char *rsh_getcwd(char *buf, size_t size){

  if ( native )
    return getcwd(buf, size);
  else
    return _rsh_getcwd(buf, size);

}

