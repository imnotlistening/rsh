/*
 * The goal of this code is to implement an incomplete wrapper for the RSH 
 * shell to facilitate writing to both the native filesystem and the built in 
 * RSH file system. The relevant calls that require a wrapper are:
 *
 *   read()
 *   write()
 *   dup2()
 *   open()
 *   close()
 *   getcwd()
 *   chdir()
 *
 * Obviously there are numerous other I/O operations that <unistd.h> supports
 * but for the sake of time, these are the ones that will actually be wrapped
 * since they are the ones needed. Yikes this will be a lot of work. But it
 * will make RSH able to access both the native file system and its own file
 * system transparently to the user of the shell. In the end, hopefully, a user
 * will be able to redirect I/O from a command executed natively to a file
 * stored on the RSH file system. What a nightmare. This is intended *only* for
 * built in commands. Commands that execute outside of the shell cannot use the
 * mechanics put in place here.
 */

#include <rsh.h>

#include <sys/types.h>

/* Definitions for RSH's version of the necessary I/O sys calls. */
ssize_t rsh_read(int fd, void *buf, size_t count);
ssize_t rsh_write(int fd, const void *buf, size_t count);
int     rsh_dup2(int oldfd, int newfd);
int     rsh_open(const char *pathname, int flags, mode_t mode);
int     rsh_close(int fd);

