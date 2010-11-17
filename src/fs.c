/*
 * Implementation from a high level perspective of a file system for RSH. See
 * <rshfs.h> and <rshio.h> for more details.
 */

#include <rsh.h>
#include <rshfs.h>
#include <rshio.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

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
#define FD_TO_FILE(fd)  ( fs.ftable[fd] )

/*
 * Our file system data. Important stuff.
 */
struct rsh_file_system fs;

/*
 * Current working directory for the RSH FS.
 */
char *rsh_cwd = "/";
int rsh_cwd_alloc = 0;
char *rsh_root;

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

  if ( ! files ){
    return RSH_ERR;
  }

  memset(files, 0, 
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

  rsh_root = (char *)malloc( strlen(leaf_name) + 1 );
  if ( ! rsh_root )
    return RSH_ERR;
  rsh_root[0] = '/';
  memcpy(rsh_root+1, leaf_name, strlen(leaf_name));

  /* Driver specific data. */
  fs.driver = driver;

  return RSH_OK;

}

/*
 * Parse a node out of a path. To parse out all nodes (and the leaf) use this
 * function like so:
 *
 *   char *node;
 *   char *path = ...; // The path string.
 *   char *copy, *next;
 *   while ( (node = _rsh_fs_parse_path(&copy, &next, path)) != NULL )
 *     // Use node
 *
 * This function will assume that the passed path is an absolute path. If not
 * then you will loose the first character in the first returned node. Just
 * make sure you use an absolute path.
 */
char *_rsh_fs_parse_path(char **copy, char **next, const char *start){

  char *end;
  char *ret;

  if ( ! *copy ){
    *copy = strdup(start);
    *next = *copy + 1; /* Dump the leading slash. */
  }

  /* Find the end of this node. */
  end = *next;
  while ( *end != 0 ){
    if ( *end == '/' )
      break;
    end++;
  }

  /* We are done. Cleanup our resources. */
  if ( *next == end ){
    free(*copy);
    *next = NULL;
    *copy = NULL;
    return NULL;
  }

  /* Otherwise, return the node of interest. */
  ret = *next;
  if ( *end )
    *next = end + 1;
  else
    *next = end; /* This will terminate the parse. */
  *end = 0;

  return ret;

}

/*
 * Returns a new string to use for the absolute pathname of a file. This string
 * must be free()'ed at some point.
 */
char *_rsh_fs_rel2abs(const char *path){

  char *fs_name;

  if ( path[0] == '/' ){
    return strdup(path);
  } else {
    fs_name = (char *)malloc(strlen(rsh_cwd) + strlen(path) + 1);
    memcpy(fs_name, rsh_cwd, strlen(rsh_cwd)+1);
    strcat(fs_name, path);
    return fs_name;
  }

}

/*
 * Turn the pesky '..' and '.' file names into what they really mean. This can
 * be done in place, thus the string you passed will get modified. It will be
 * either exactly as long as it was to start with or shorter (think about it).
 * This function *will not* work with a relative path.
 */
void _rsh_fs_interpolate(char *path){

  char *node;
  char *copy = NULL, *next;
  char *rpath;
  char *rpath_start = (char *)malloc(strlen(path));
  memset(rpath_start, 0, strlen(path));
  rpath = rpath_start;

  while ( (node = _rsh_fs_parse_path(&copy, &next, path)) != NULL ){

    /*
     * There are three possibilities. Either the node is '.' in which case we
     * just do nothing, or the node is '..' in which case we chop the last node
     * of the rpath, or the node is neither '.' or '..' in which case we
     * append the node to the rpath. Yeesh.
     */
    if ( strcmp(node, ".") == 0 )
      continue;

    if ( strcmp(node, "..") == 0 ){
      
      while ( *rpath != '/' ){
	if ( rpath == rpath_start ) 
	  break;
	rpath--;
      }

      *rpath = 0; /* Null terminate. */

    } else {
      
      strcat(rpath, "/");
      strcat(rpath, node);
      while ( *rpath++ != '/' );
      rpath++;

    }

  }

  /* We have removed all dots at this point. */
  memcpy(path, rpath_start, strlen(rpath_start)+1);
  free(rpath_start);

  /* This happens if we end up at the root directory. Just put in /. */
  if ( ! *path ){
    *path++ = '/';
    *path = 0;
  }

}

int _rsh_chdir(const char *dir){

  int fd;
  int ret;
  char *full_path, *tmp;
  struct stat buf;

  if ( *dir != '/' )
    tmp = _rsh_fs_rel2abs(dir);
  else
    tmp = strdup(dir);

  if ( ! tmp ){
    errno = ENOMEM;
    return RSH_ERR;
  }

  _rsh_fs_interpolate(tmp);

  full_path = (char *)malloc(strlen(tmp) + 2);
  memcpy(full_path, tmp, strlen(tmp)+1);

  if ( full_path[strlen(full_path)-1] != '/' ){
    full_path[strlen(full_path)+1] = 0;
    full_path[strlen(full_path)] = '/';
  }

  fd = _rsh_open(full_path, 0, 0);
  if ( fd < 0 ){
    ret = RSH_ERR;
    goto cleanup;
  }

  ret = _rsh_fstat(fd, &buf);
  if ( ret < 0 ){
    ret = RSH_ERR;
    goto cleanup;
  }
  rsh_close(fd);

  if ( ! (buf.st_mode & S_IFDIR) ){
    errno = ENOTDIR;
    ret = RSH_ERR;
    goto cleanup;
  }

  if ( rsh_cwd_alloc )
    free(rsh_cwd);

  rsh_cwd = full_path;
  rsh_cwd_alloc = 1;

  return RSH_OK;

 cleanup:
  free(full_path);
  free(tmp);
  return ret;

}

char *_rsh_getcwd(char *buf, size_t size){

  char *_buf = NULL;

  if ( ! size && buf ){
    errno = EINVAL;
    return NULL;    
  }

  if ( buf )
    _buf = buf;
  else
    _buf = (char *)malloc(size);

  if ( ! _buf ){
    errno = ENOMEM;
    return NULL;
  }

  if ( strlen(rsh_cwd) >= size ){
    if ( ! buf )
      free(_buf);
    errno = ERANGE;
    return NULL;
  }

  memcpy(_buf, rsh_cwd, size);
  if ( _buf[strlen(_buf)-1] == '/' && strcmp(_buf, "/") != 0 )
    _buf[strlen(_buf)-1] = 0;

  return _buf;

}

extern int native;
/*
 * Return > 0 if the specified path is located on the native file system. If
 * the path is a relative path, then the path will be interpretted as native if
 * RSH is currently using the naitve file system as default or visa versa.
 */
int rsh_path_location(char *path){

  /* Built in FS path. */
  if ( strncmp(path, rsh_root, strlen(rsh_root)) == 0 )
    return 0;
  
  /* Native path. */
  if ( path[0] == '/' )
    return 1;
  
  /* If the path is relative, it will be relative to the current FS. */
  return native;

}

ssize_t _rsh_read(int fd, void *buf, size_t count){

  if ( ! _RSH_FD(fd) ){
    errno = EBADF;
    return RSH_ERR;
  }
  fd = _RSH_FD_TO_INDEX(fd);

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

  if ( ! _RSH_FD(fd) ){
    errno = EBADF;
    return RSH_ERR;
  }
  fd = _RSH_FD_TO_INDEX(fd);

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
  char *fs_name;
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
  if ( ! file ){
    return RSH_ERR;
  }

  fs_name = _rsh_fs_rel2abs(pathname);

  /* Fill out this file struct and pass it on to the FS driver. */
  memset(file, 0, sizeof(struct rsh_file));
  file->used = 1;
  file->references = 1;
  file->offset = 0;
  file->path = fs_name;
  file->fops = fs.fops;

  if ( fs.fops->open )
    err = fs.fops->open(file, file->path, flags);
  else
    err = 0;

  if ( ! err ){
    fs.used++;
    return fd | _RSH_FD_OFFSET;
  } else {
    file->used = 0;
    free(file->path);
    return err;
  }

}

int _rsh_close(int fd){

  int ret;

  if ( ! _RSH_FD(fd) ){
    errno = EBADF;
    return RSH_ERR;
  }
  fd = _RSH_FD_TO_INDEX(fd);

  /* Check to make sure this is actually an open file. */
  if ( ! fs.ftable[fd].used ){
    errno = EBADF;
    return RSH_ERR;
  }
  
  /* Now we do the usual. */
  fs.ftable[fd].references -= 1;

  if ( fs.ftable[fd].references == 0 ){
    fs.ftable[fd].used = 0;
    fs.used--;
    if ( fs.fops->close ){
      ret = fs.fops->close(FD_TO_FPTR(fd));
      free(FD_TO_FILE(fd).path);
      return ret;
    } else {
      return RSH_OK;
    }
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

  if ( ! _RSH_FD(dfd) ){
    errno = EBADF;
    return NULL;
  }
  dfd = _RSH_FD_TO_INDEX(dfd);

  /* Check to make sure this is actually an open file. */
  if ( ! fs.ftable[dfd].used ){
    errno = EBADF;
    return NULL;
  }

  /* Make sure the file system supports this operation. */
  if ( ! fs.fops->readdir ){
    errno = ENOSYS;
    return NULL;
  }

  /* If we need more dir entries, then get some more. */
  if ( dirs_len == 0 ){

    /* This will be set if the last directory read read the last of the dir
     * entries. Hehe, this comment is less understandable than the code. */
    if ( almost_done )
      goto reset;

    dirs_len = fs.fops->readdir(FD_TO_FPTR(dfd), dirs, 
				DIRS_PER_READ * sizeof(struct dirent));

    /* Apparently we are done since there are no more dir entries. Or maybe we
     * are almost done. */
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
    diri = 0;
    dirs_len = 0;
  }

  /* Finally... */
  return &dir_entry;

  /* Reset all of the peices of state information and the like. Then return
   * NULL. */
  reset:
  diri = 0;
  dirs_len = 0;
  almost_done = 0;
  return NULL;

}

int _rsh_fstat(int fd, struct stat *buf){

  if ( ! _RSH_FD(fd) ){
    errno = EBADF;
    return RSH_ERR;
  }
  fd = _RSH_FD_TO_INDEX(fd);

  if ( ! fs.ftable[fd].used ){
    errno = EBADF;
    return RSH_ERR;
  }

  /* Fill in the relevant data fields. */
  buf->st_dev = 0;                 /* No dev ID since we are in userspace. */
  buf->st_ino = 0;                 /* Same idea, were in user space. */
  buf->st_mode = FD_TO_FILE(fd).mode;
  buf->st_nlink = 1;
  buf->st_uid = getuid();
  buf->st_gid = getgid();
  buf->st_rdev = 0;                /* No special files. */
  buf->st_size = FD_TO_FILE(fd).size;
  buf->st_blksize = FD_TO_FILE(fd).block_size;
  buf->st_blocks = FD_TO_FILE(fd).blocks;
  buf->st_atime = buf->st_mtime =  0; /* Not kept track of. */
  buf->st_ctime = FD_TO_FILE(fd).access_time;

  return RSH_OK;

}

int _rsh_mkdir(const char *path){

  char *fs_name = _rsh_fs_rel2abs(path);

  if ( fs.fops->mkdir ){
    return fs.fops->mkdir(fs_name);
  } else {
    errno = EPERM;
    return RSH_ERR;
  }

}

int _rsh_unlink(const char *path){

  char *fs_name = _rsh_fs_rel2abs(path);

  if ( fs.fops->unlink )
    return fs.fops->unlink(fs_name);
  else
    return 0;

}

int builtin_dumpfds(int argc, char **argv, int in, int out, int err){

  int i;

  for ( i = 0; i < fs.ft_length; i++){

    if ( ! fs.ftable[i].used )
      continue;
    
    printf("FD: %d\n", i);
    printf("  references: %d\n", (int)fs.ftable[i].references);
    printf("  offset:     %d\n", (int)fs.ftable[i].offset);
    printf("  path:       %s\n", fs.ftable[i].path);
    printf("  size:       %d\n", (int)fs.ftable[i].size);

  }

  return 0;

}
