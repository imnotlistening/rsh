/*
 * Implementation from a high level perspective of a file system for RSH. See
 * <rshfs.h> and <rshio.h> for more details.
 */

#include <rsh.h>
#include <rshfs.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FILES_BLOCK_INC 8
#define DIRS_PER_READ   8

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
  printf(" <> Found an open FD: %d\n", fd);

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
    err = fs.fops->open(file, pathname, flags);
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

/*
 * A buffer for holding directory information. Also a variable to tell us the
 * index into this array we are currently at.
 */
struct dirent dirs[DIRS_PER_READ];
struct dirent dir_entry;
int diri = 0;
int dirs_len = 0;
int almost_done = 0;

/*
 * Read a directory listing. This is a pain in the ass.
 */
struct dirent *_rsh_readdir(int dfd){

  errno = 0;

  printf("  Am I being called?\n");

  /* Check to make sure this is actually an open file. */
  if ( ! fs.ftable[dfd].used ){
    errno = EBADF;
    printf("BAD.\n");
    return NULL;
  }

  /* Make sure the file system supports this operation. */
  if ( ! fs.fops->readdir ){
    errno = ENOSYS;
    return NULL;
  }

  /* If we need more dir entries, then get some more. */
  if ( dirs_len == 0 ){

    printf("  | Getting more dir entries from disk.\n");
    /* This will be set if the last directory read read the last of the dir
     * entries. Hehe, this comment is less understandable than the code. */
    if ( almost_done )
      goto reset;

    printf("  | Doing read.\n");
    dirs_len = fs.fops->readdir(FD_TO_FPTR(dfd), dirs, 
				DIRS_PER_READ * sizeof(struct dirent));

    /* Apparently we are done since there are no more dir entries. Or maybe we
     * are almost done. */
    printf("  | Read %d dir entries.\n", dirs_len);
    if ( dirs_len == 0 )
      goto reset;
    else if ( dirs_len < DIRS_PER_READ )
      almost_done = 1;

  }

  /* Read a directory entry from our buffer into dir_entry. Handle the wrap
   * around here as well. */
  dir_entry = dirs[diri++];

  /* This askes the next call to this function to read more dirents */
  if ( diri >= dirs_len ){
    printf("  |Requesting more dir entries for later.\n");
    diri = 0;
    dirs_len = 0;
  }

  /* Finally... */
  return &dir_entry;

  /* Reset all of the peices of state information and the like. Then return
   * NULL. */
  reset:
  printf("  | Done with that dir listing.\n");
  diri = 0;
  dirs_len = 0;
  almost_done = 0;
  return NULL;

}
