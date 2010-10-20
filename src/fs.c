/*
 * Implementation from a high level perspective of a file system for RSH. See
 * <rshfs.h> and <rshio.h> for more details.
 */

#include <rsh.h>
#include <rshfs.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define FILES_BLOCK_INC 8

#define LOCATE_FILE(idx, fptr)			\
  do {						\
    for ( idx = 0; idx < fs.ft_length; idx++){	\
      if ( ! fs.ftable[idx].used ){		\
        fptr = &(fs.ftable[idx]);		\
        break;					\
      }						\
    }						\
  }  while (0)

#define FD_TO_FPTR(idx) ( &(fs.ftable[idx]) )

/*
 * Our file system data. Important stuff.
 */
struct rsh_file_system fs;

/*
 * Current working directory for the RSH FS.
 */
char *rsh_cwd;

int rsh_init_fs(){

  struct rsh_file *files = (struct rsh_file *)
    malloc(sizeof(struct rsh_file) * FILES_BLOCK_INC );

  if ( ! files ) 
    return RSH_ERR;

  fs.ftable = files;
  fs.ft_length = FILES_BLOCK_INC;
  fs.used = 0;
  memset(fs.ftable, 0, sizeof(struct rsh_file) * FILES_BLOCK_INC );

  fs.fops = NULL;

  return RSH_OK;

}

int _rsh_increase_files(){

  struct rsh_file *files = (struct rsh_file *)
    malloc(sizeof(struct rsh_file) * ( fs.ft_length + FILES_BLOCK_INC) );

  if ( ! files ) 
    return RSH_ERR;

  memset(fs.ftable, 0, 
	 sizeof(struct rsh_file) * ( fs.ft_length + FILES_BLOCK_INC) );

  memcpy(files, fs.ftable, sizeof(struct rsh_file) * fs.ft_length );
  free(fs.ftable);
  fs.ftable = files;
  fs.ft_length += FILES_BLOCK_INC;

  return RSH_OK;

}

int rsh_register_fs(struct rsh_io_ops *fops, char *name, void *driver){
  
  int i;
  char *leaf_name;

  /* Don't overwrite a previous file system. */
  if ( fs.fops != NULL ){
    return RSH_ERR;
  }

  /* Otherwise, just replace the file system's fops structure. */
  fs.fops = fops;

  /* And handle directory stuff. */
  leaf_name = name;
  for ( i = strlen(name); i >= 0; i--){
    if ( name[i] == '/' ){
      leaf_name = &(name[i+1]);
      break;
    }
  }

  rsh_cwd = (char *)malloc( strlen(leaf_name) + 1 );
  if ( ! rsh_cwd )
    return RSH_ERR;
  rsh_cwd[0] = '/';
  memcpy(rsh_cwd+1, leaf_name, strlen(leaf_name));

  /* Driver specific data. */
  fs.driver = driver;

  return RSH_OK;

}

ssize_t _rsh_read(int fd, void *buf, size_t count){

  /* Check to make sure this is actually an open file. */
  if ( ! fs.ftable[fd].used ){
    errno = EBADF;
    return RSH_ERR;
  }

  if ( fs.fops->read )
    return fs.fops->read(FD_TO_FPTR(fd), buf, count);
  else
    return 0;

}

ssize_t _rsh_write(int fd, const void *buf, size_t count){

  /* Check to make sure this is actually an open file. */
  if ( ! fs.ftable[fd].used ){
    errno = EBADF;
    return RSH_ERR;
  }

  if ( fs.fops->write )
    return fs.fops->write(FD_TO_FPTR(fd), buf, count);
  else
    return 0;

}

int _rsh_dup2(int oldfd, int newfd){

  return RSH_ERR;

}

int _rsh_open(const char *pathname, int flags, mode_t mode){

  int fd;
  int err;
  struct rsh_file *file = NULL;

  /* First things first, make sure we have enough room to allocate another
   * file struct. */
  while ( fs.used >= fs.ft_length ){
    err = _rsh_increase_files();
    if ( err == RSH_ERR ) /* Bad, memory allocator, bad. */
      return RSH_ERR;
  }

  /* Now get down to business. Find an open file slot. */
  LOCATE_FILE(fd, file);

  /* This would be a bug... */
  if ( ! file )
    return RSH_ERR;

  /* Fill out this file struct and pass it on to the FS driver. */
  memset(file, 0, sizeof(struct rsh_file));
  file->used = 1;
  file->references = 1;
  file->offset = 0;
  file->path = pathname;
  file->fops = fs.fops;

  if ( fs.fops->open )
    err = fs.fops->open(file, pathname);
  else
    err = 0;

  if ( ! err )
    return fd;
  else
    return err;

}

int _rsh_close(int fd){

  /* Check to make sure this is actually an open file. */
  if ( ! fs.ftable[fd].used ){
    errno = EBADF;
    return RSH_ERR;
  }
  
  /* Now we do the usual. */
  fs.ftable[fd].references -= 1;

  if ( fs.ftable[fd].references == 0 ){
    fs.ftable[fd].used = 0;
    if ( fs.fops->close )
      return fs.fops->close(FD_TO_FPTR(fd));
    else
      return RSH_OK;
  } else {
    return RSH_OK;
  }

}
