/*
 * A FAT16 implementation that can be utilised by RSH. Oh well.
 */

#include <rsh.h>
#include <rshfs.h>

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>

struct rsh_fat16_fs fat16_fs;

#define FAT_CLUSTER_TO_ADDR(cluster)		\
  ( fat16_fs.fs_io + (cluster * fat16_fs.fs_header.csize) )
#define FAT_WIPE_CLUSTER(cluster_io_addr)	\
  ( memset(cluster_io_addr, 0, fat16_fs.fs_header.csize) )

#define FAT_CLUSTER_SIZE (fat16_fs.fs_header.csize)

/*
 * This is a *bad* function. It should only be called when there is irrevocable
 * corruption to the file system. Tihs will cause a slow and painful death for
 * the shell: AAARrrghhh.
 */
void rsh_fat16_badness(){

  printf("FAT16 filesystem irrevocably corrupt. Cry moar.\n");
  printf("FAT table:\n");
  _rsh_fat16_display_fat();
  exit(1);

}

/*
 * Split a path. This will insert a null byte in the passed path.
 */
char *_rsh_fat16_split_path(char *path, char **name){

  int len = strlen(path);

  while ( len-- > 0 ){
    if ( path[len] != '/' )
      continue;

    path[len] = 0;
    *name = path + len + 1;
    return path;

  }

  return NULL;

}

/*
 * Trace a file through the FAT. Head is the starting location, offset is the
 * maximum allowed jumps. If the particular file in the FAT ends before offset
 * jumps, the index of the last fat accessed is returned. Otherwise, the index
 * 'offset' jumps from head is returned.
 *
 * If a negative number is passed for the offset, find the last cluster in the
 * chain.
 */
fat_t _rsh_fat16_follow_head(uint32_t head, uint32_t offset){

  fat_t fat_index;
  fat_t next_index;

  fat_index = head;
  while ( offset-- != 0 ){

    next_index = rsh_fat16_get_entry(fat_index);
    
    if ( next_index == FAT_TERM )
      break;

    /* This is bad. */
    if ( next_index == FAT_RESERVED || next_index == FAT_FREE )
      rsh_fat16_badness();

    fat_index = next_index;

  }

  return fat_index;

}

/*
 * Finds an open cluster. Thats it. The cluster you get may be filled with
 * crap. Call _rsh_fat16_find_open_cluster() to get a cleared cluster. This
 * requires a write to the disk drive though.
 */
int __rsh_fat16_find_open_cluster(uint32_t *addr){

  uint32_t i;
  fat_t entry;

  /* Iterate through the FAT. */
  for ( i = 0; i < fat16_fs.fat_entries; i++){

    entry = rsh_fat16_get_entry(i);
    if ( entry == FAT_FREE ){
      *addr = i;
      return RSH_OK;
    }

  }

  return RSH_ERR;

}

/*
 * Find and return the first available cluster. This will only fail if there
 * are no more free clusters in the file system. :(. On success, *addr will
 * contain the cluster address.
 */
int _rsh_fat16_find_open_cluster(uint32_t *addr){

  int err = __rsh_fat16_find_open_cluster(addr);
  if ( err )
    return err;

  FAT_WIPE_CLUSTER(FAT_CLUSTER_TO_ADDR(*addr));
  return err;

}

/*
 * Locate a child node in the specified directory table. It is assumed that the
 * cluster pointed to by dir_table is the start of the dir table. We will 
 * assume the calling function did proper error checking. If no child with the 
 * specified name was found then NULL is returned. Otherwise, the address of
 * the child's dirent on the file system is returned.
 */
struct rsh_fat_dirent *_rsh_fat16_locate_child(const char *child, 
					       uint32_t dir_table){

  int i;
  uint32_t clust = dir_table;
  struct rsh_fat_dirent *child_addr = NULL;
  struct rsh_fat_dirent *dir_entries;

  do {

  start:
    /* Search the cluster. */
    dir_entries = FAT_CLUSTER_TO_ADDR(clust);
    for ( i = 0; i < fat16_fs.dir_per_cluster; i++){

      /* We have a winner. */
      if ( strcmp(child, dir_entries[i].name) == 0 ){
	child_addr = &(dir_entries[i]);
	break;
      }

    }
    
    /* If we found the child, we are done. */
    if ( child_addr )
      break;

    /* And increment the clust address (if necessary). */
    if ( rsh_fat16_get_entry(clust) != FAT_TERM ){
      clust = rsh_fat16_get_entry(clust);
      goto start;
    }

  } while ( rsh_fat16_get_entry(clust) != FAT_TERM );

  return child_addr;

}

/*
 * Locate an empty dirent spot in a directory table.
 */
inline struct rsh_fat_dirent *_rsh_fat16_find_open_dirent(uint32_t dir_tbl){

  char empty = 0;
  
  return _rsh_fat16_locate_child(&empty, dir_tbl);

}

/*
 * Parse a node out of a path. To parse out all nodes (and the leaf) use this
 * function like so:
 *
 *   char *node;
 *   char *path = ...; // The path string.
 *   char *copy, *next;
 *   while ( (node = _rsh_fat16_parse_path(&copy, &next, path)) != NULL )
 *     // Use node
 *
 * This function will assume that the passed path is an absolute path. If not
 * then you will loose the first character in the first returned node. Just
 * make sure you use an absolute path.
 */
char *_rsh_fat16_parse_path(char **copy, char **next, const char *start){

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
  *end = 0;
  *next = end + 1;

  return ret;

}

/*
 * Take a path and find the dir entry that corresponds to that path. The dirent
 * is copied into the rsh_fat_dirent struct passed to this function in ent. If
 * an error occurs, then a negative return value will result. Check errno for
 * the specific error.
 */
int _rsh_fat16_path_to_dirent(const char *path, struct rsh_fat_dirent *ent,
			      struct rsh_fat_dirent **fs_addr){

  uint32_t dir_tbl = fat16_fs.fs_header.root_offset;
  struct rsh_fat_dirent *child;

  /* Start with the root directory entry and start looking up path nodes. */
  char *node;
  char *copy = NULL, *next = NULL;
  while ( (node = _rsh_fat16_parse_path(&copy, &next, path)) != NULL){

    child = _rsh_fat16_locate_child(node, dir_tbl);
    if ( ! child ){
      errno = ENOENT;
      return -1;
    }

    /* We have a directory as part of the path, just set the dir_tbl to the
     * new directorie's table, and try again. */
    if ( child->type == FAT_DIR ){
      dir_tbl = child->index;
      continue;
    }

    /* Check if we have a corrupt file system. */
    if ( child->type != FAT_FILE )
      rsh_fat16_badness();

    /* OK, we have a regular file node. This is a problem if there are more
     * nodes in the path. */
    if ( _rsh_fat16_parse_path(&copy, &next, path) != NULL ){
      errno = ENOTDIR;
      return -1;
    } else {
      break;
    }

  }

  /* If we are here, then the dirent is valid. */
  if ( ent)
    *ent = *child;
  if ( fs_addr )
    *fs_addr = child;

  return 0;

}

void _rsh_fat16_wipe_file(struct rsh_fat_dirent *dirent){

  fat_t prev;
  fat_t current;

  /* Set the first entry in the chain to FAT_TERM. We want to keep at least
   * one cluster for use. */
  current = rsh_fat16_get_entry(dirent->index);
  rsh_fat16_set_entry(dirent->index, FAT_TERM);
  if ( current == FAT_TERM )
    return;

  /* Iterate across a FAT table chain, wiping the FAT entries. */
  current = rsh_fat16_get_entry(current);
  prev = current;
  do {

    current = rsh_fat16_get_entry(prev);
    rsh_fat16_set_entry(prev, FAT_FREE);

    /* We are done. */
    if ( current == FAT_TERM )
      break;
    
    /* This is BADNESS!!! */
    if ( current == FAT_FREE )
      rsh_fat16_badness();

    prev = current;

  } while ( 1 );

}

/*
 * Locate the index into the FAT table of the last cluster in a file.
 * file_start is the index of the file's first cluster.
 */
uint32_t _rsh_fat16_get_last_cluster(uint32_t file_start){

  uint32_t next;
  uint32_t current = file_start;

  for ( ; ; ){

    next = rsh_fat16_get_entry(current);
    if ( next == FAT_TERM )
      return current;

    if ( next == FAT_FREE || next == FAT_RESERVED )
      rsh_fat16_badness();

    current = next;

  }

}

/*
 * Return the address in the FAT table of the cluster index clusters down the
 * chain from head.
 */
uint32_t _rsh_fat16_find_cluster_index(uint32_t head, int index){

  int cur_index = 0;
  uint32_t current = head;

  for ( ; ; ){

    if ( cur_index == index )
      return current;

    current = rsh_fat16_get_entry(current);
    if ( current == FAT_TERM )
      return FAT_TERM;

    if ( current == FAT_FREE || current == FAT_RESERVED )
      rsh_fat16_badness();

    cur_index++;

  }

}

/*
 * Read data from a file. Store that data in buf. Return the number of bytes
 * read.
 */
ssize_t rsh_fat16_read(struct rsh_file *file, void *buf, size_t count){

  int xfer_size;
  int remaining = count;
  void *buffer = buf;
  void *cluster_io;
  uint32_t cluster;
  uint32_t clusters;
  uint32_t cluster_addr;
  uint32_t cluster_offset;
  struct rsh_fat_dirent *file_ent = file->local;

  /* Figure out how many clusters are in the file. */
  clusters = file_ent->size / FAT_CLUSTER_SIZE;
  if ( clusters * FAT_CLUSTER_SIZE < file_ent->size )
    clusters++;
  if ( file_ent->size == 0 )
    clusters = 1;

  printf("  | File clusters: %u\n", clusters);

  while ( remaining > 0 && (file->offset < file_ent->size) ){

    /* First find the source cluster. */
    cluster = file->offset / FAT_CLUSTER_SIZE;
    cluster_offset = file->offset % FAT_CLUSTER_SIZE;
    cluster_addr = _rsh_fat16_find_cluster_index(file_ent->index, cluster);
    cluster_io = FAT_CLUSTER_TO_ADDR(cluster_addr) + cluster_offset;
    xfer_size = FAT_CLUSTER_SIZE - cluster_offset;
    if ( xfer_size > remaining )
      xfer_size = remaining;
    if ( (file_ent->size - file->offset) < xfer_size )
      xfer_size = file_ent->size - file->offset;

    printf("  | Remaining bytes: %d\n", remaining);
    printf("  | Current cluster offset: %u\n", cluster);
    printf("  | Cluster address: %u\n", cluster_addr);
    printf("  | cluster offset: %u\n", cluster_offset);
    printf("  | xfer size: %d\n", xfer_size);

    /* Now we can do the transfer. */
    memcpy(buffer, cluster_io, xfer_size);

    /* Do some book keeping. */
    file->offset += xfer_size;
    remaining -= xfer_size;
    buffer += xfer_size;

    printf("  | Book keeping: file->offset = %u & remaining = %d\n", 
	   (uint32_t) file->offset, remaining);

  }

  return count - remaining;

}

/*
 * Write some data to a file. We are passed a rsh_file struct that describes
 * the file. All required data for accessing the file should be in the passed
 * struct rsh_file local pointer. This will fail if there is not enough space.
 * Returns the number of bytes written. If a negative value is returned then
 * there was an errer; check errno for the actual error.
 */
ssize_t rsh_fat16_write(struct rsh_file *file, const void *buf, size_t count){

  int err;
  int remaining = count;
  int xfer_size;
  void *cio;
  void *cmem;
  uint32_t cluster;
  uint32_t cluster_addr;
  uint32_t cluster_offset;
  uint32_t cluster_index;
  uint32_t clusters;
  const void *buffer = buf;
  struct rsh_fat_dirent *file_ent = file->local;

  cmem = malloc(FAT_CLUSTER_SIZE);
  if ( ! cmem ){
    errno = ENOMEM;
    return -1;
  }

  /* Figure out how many clusters are in the file. */
  clusters = file_ent->size / FAT_CLUSTER_SIZE;
  if ( clusters * FAT_CLUSTER_SIZE < file_ent->size )
    clusters++;
  if ( file_ent->size == 0 )
    clusters = 1;
  
  printf("  | clusters in file: %u\n", clusters);

  /* Deal with the write. */
  while ( remaining > 0 ){

    cluster = file->offset / FAT_CLUSTER_SIZE;
    printf("  | writing to cluster %u\n", cluster);

    /* First we prepare the source cluster for the transfer. We will only
     * write to the FS in buffers equal to the cluster size. This makes the
     * write simpler. If the file pointer is not on a cluster boundary, we
     * must first fill the source cluster with the necessary bytes to preserve
     * the data in the destination cluster. */
    cluster_offset = file->offset % FAT_CLUSTER_SIZE;
    xfer_size = remaining;
    if ( xfer_size > FAT_CLUSTER_SIZE )
      xfer_size = FAT_CLUSTER_SIZE - cluster_offset;
    printf("  | cluster_offset = %u & xfer_size = %d\n", 
	   cluster_offset, xfer_size);

    /* Back up the cluster's storage if we have to. */
    if ( cluster_offset )
      memcpy(cmem, 
	     FAT_CLUSTER_TO_ADDR(_rsh_fat16_find_cluster_index(file_ent->index,
							       cluster)), 
	     FAT_CLUSTER_SIZE);

    /* Now copy relevant bytes from the passed buffer into cmem. */
    memcpy(cmem + cluster_offset, buffer, xfer_size);
    
    /* We are now ready to prepare the destination cluster. The goal is to find
     * an IO address we can copy to. This means we need to figure out how many
     * clusters into the file we are and then determine if that cluster exists.
     * If the cluster does not yet exist, we must allocate one. */
    cluster_addr = _rsh_fat16_find_cluster_index(file_ent->index, cluster);
    printf("  | cluster_addr = %x\n", cluster_addr);

    /* If we need to allocate a new cluster, do so here. */
    if ( cluster_addr == FAT_TERM ){
      printf("  |   Allocating new cluster...\n");
      cluster_index = _rsh_fat16_follow_head(file_ent->index, -1);
      err = __rsh_fat16_find_open_cluster(&cluster_addr);
      if ( err ){
	errno = ENOSPC;
	return -1;
      }
      rsh_fat16_set_entry(cluster_index, cluster_addr);
      rsh_fat16_set_entry(cluster_addr, FAT_TERM);
      printf("  | cluster_addr = %x\n", cluster_addr);
    }

    /* We have the cluster address, get an IO address. */
    cio = FAT_CLUSTER_TO_ADDR(cluster_addr);
    memcpy(cio, cmem, FAT_CLUSTER_SIZE);

    /* And some book keeping. */
    remaining -= xfer_size;
    file->offset += xfer_size;
    if ( file->offset > file_ent->size )
      file_ent->size = file->offset;
    printf("  | Book keeping:\n");
    printf("  |   remaining = %d ; size = %u ; offset = %u\n",
	   remaining, file_ent->size, (unsigned int)file->offset);

  }

  /* Cleanup. */
  free(cmem);

  return count;

}

/*
 * Read some amount of a directory entry into the specified buffer. This
 * function need not be reentrant. In fact this function is not reentrant at
 * all, and I have no desire to implement a reentrant one, it just isn't worth
 * it. This function will return the number of directory entries placed into
 * the buffer.
 */
int rsh_fat16_readdir(struct rsh_file *file, void *buf, size_t space){

  int ents;
  int ents_per_cluster = FAT_CLUSTER_SIZE / sizeof(struct rsh_fat_dirent);
  uint32_t cluster;
  uint32_t ent_index;
  uint32_t cluster_addr;
  struct dirent *dirent_p = buf;
  struct rsh_fat_dirent *dir_entries;
  struct rsh_fat_dirent *file_ent = file->local;
  
  /* This is the state information. */
  static int next_dirent = 0;

  /* Verify the file descriptor. */
  if ( file_ent->type != FAT_DIR ){
    errno = EBADF;
    return -1;
  }

  /* Figure out how many entires we should transfer. */
  ents = space / sizeof(struct dirent);

  printf("  | Copying at most %d ents to buffer.\n", ents);
  printf("  | Next dirent to copy: %d\n", next_dirent);

  while ( ents > 0 ){

    /* Based on next_dirent, we can figure which cluster we should read the
     * dirent from and how far into that cluster the dirent is stored. */
    cluster = next_dirent / ents_per_cluster;
    ent_index = next_dirent % ents_per_cluster;

    printf("  | Reading from cluster: %u, offset: %u\n", cluster, ent_index);

    /* Now get the actual I/O memory address of the cluster. */
    cluster_addr = _rsh_fat16_find_cluster_index(file_ent->index, cluster);
    if ( cluster_addr == FAT_TERM ){
      /* Reset everything. */
      next_dirent = 0;
      break;
    }
    dir_entries = (struct rsh_fat_dirent *) FAT_CLUSTER_TO_ADDR(cluster_addr);

    if ( ! dir_entries[ent_index].name[0] ){
      printf("  | Empty dirent, going to next.\n");
      break;
    }
    
    /* Now just do a copy. */
    memset(dirent_p, 0, sizeof(struct dirent));
    memcpy(dirent_p->d_name, dir_entries[ent_index].name, 112);
    ents--;
    printf("Remaining ents: %d\n", ents);

  }

  return (space / sizeof(struct dirent)) - ents;

}

/*
 * Handle the gory details of opening a file in the file system. Populate the
 * relevant file fields. The relevant options that this file system supports
 * are as follows:
 *
 *   O_APPEND
 *   O_CREAT
 *   O_TRUNC
 *
 * See the man page for open() for more details as to what these flags do. Any
 * other flags have an undefined responce.
 */
int rsh_fat16_open(struct rsh_file *file, const char *pathname, int flags){

  int err;
  char *dir, *name;
  char *copy;
  struct rsh_fat_dirent dirent;
  struct rsh_fat_dirent *child;
  
  copy = strdup(pathname);
  if ( ! copy ){
    errno = ENOMEM;
    return -1;
  }
  dir = _rsh_fat16_split_path(copy, &name);

  /* Set the file's local pointer to the address of this file's dirent on the
   * file system. Thats all we need to do, but this is a little more complex
   * than it first appears. */
  if ( *dir ){
    err = _rsh_fat16_path_to_dirent(dir, &dirent, NULL);
    if ( err )
      return err;
  } else {
    dirent = *(_rsh_fat16_locate_child(".", fat16_fs.fs_header.root_offset));
  }
  printf("open: Got directory table: %u\n", dirent.index);

  /* Now dirent is filled out, we should think about the file... */
  if ( *name ){

    child = _rsh_fat16_locate_child(name, dirent.index);
    if ( ! child ){
      /* The child doesn't exist, should we create one for the user? */
      if ( flags & O_CREAT ){
	_rsh_fat16_mkfile(dirent.index, name);
      } else {
	errno = ENOENT;
	return -1;
      }
      child = _rsh_fat16_locate_child(name, dirent.index);
    }
    printf("open: Found a suitable child node.\n");

    /* Now, based on O_APPEND and O_TRUNC we should act accordingly. */
    if ( flags & O_APPEND && flags & O_TRUNC ){
      errno = EINVAL;
      return -1;
    }

    if ( flags & O_APPEND ){
      printf("open: Opening for appending.\n");
      file->offset = child->size;
    }
    if ( flags & O_TRUNC ){
      printf("open: truncating.\n");
      file->offset = 0;
      _rsh_fat16_wipe_file(child);
    }

    /* This will over write data. Not if its a directory though. */
    file->offset = 0;

  } else {
    child = _rsh_fat16_locate_child(".", dirent.index);
  }

  file->local = child;

  return 0;

}

/*
 * Close a file. Release any reousrces associated with the file that must be
 * released. This will also msync all data to the disk. The kernel will handle
 * the caching though, so this should only write data that actually *needs* to
 * be written.
 */
int rsh_fat16_close(struct rsh_file *file){

  struct rsh_fat_dirent *file_ent = file->local;
  fat_t entry = file_ent->index;

  while ( entry != FAT_TERM ){

    /* Sync the data out to disk. */
    msync(FAT_CLUSTER_TO_ADDR(entry), FAT_CLUSTER_SIZE, MS_SYNC);

    /* Now update entry */
    entry = rsh_fat16_get_entry(entry);
    if ( entry == FAT_FREE || entry == FAT_RESERVED )
      rsh_fat16_badness();

  }

  return 0;

}

/*
 * Make a directory entry. We need the directory table to put it in and the
 * name of the directory, thats it.
 */
int _rsh_fat16_mkdir(uint32_t dir_table, const char *name){

  int err;
  uint32_t tail;
  uint32_t cluster;
  uint32_t dir_cluster;
  struct rsh_fat_dirent *slot;
  struct rsh_fat_dirent *dot;
  struct rsh_fat_dirent *dotdot;

  /* Make sure there is no directory of the asked name. */
  if ( _rsh_fat16_locate_child(name, dir_table) ){
    errno = EEXIST;
    return -1;
  }

  /* First we need to make sure there is room for another dirent. */
  slot = _rsh_fat16_find_open_dirent(dir_table);
  if ( ! slot ){
    err = _rsh_fat16_find_open_cluster(&cluster);
    if ( err < 0 ){
      errno = ENOSPC;
      return RSH_ERR;
    }
    /* Tack this cluster onto the dir table. */
    tail = _rsh_fat16_follow_head(dir_table, -1);
    rsh_fat16_set_entry(tail, cluster);
    rsh_fat16_set_entry(cluster, FAT_TERM);
    slot = _rsh_fat16_find_open_dirent(dir_table);
  }

  /* Now we have a slot. Get a cluster for the directory. */
  err = _rsh_fat16_find_open_cluster(&dir_cluster);
  if ( err < 0 ){
    errno = ENOSPC;
    return RSH_ERR;
  }
  rsh_fat16_set_entry(dir_cluster, FAT_TERM);
  
  /* Fill in the directory entry. */
  strncpy(slot->name, name, 112);
  slot->name[111] = 0; /* Make sure its null terminated. */
  slot->index = dir_cluster;
  slot->size = 0;
  slot->type = FAT_DIR;
  slot->epoch = (uint32_t) time(NULL);

  /* Fill in the dot and dotdot entries. */
  dot = FAT_CLUSTER_TO_ADDR(dir_cluster);
  dotdot = FAT_CLUSTER_TO_ADDR(dir_cluster) + sizeof(struct rsh_fat_dirent);

  /* dot points to ourselves. */
  strcpy(dot->name, ".");
  dot->index = dir_cluster;
  dot->size = 0;
  dot->type = FAT_DIR;
  dot->epoch = (uint32_t) time(NULL);

  /* dotdot points to our parent. */
  strcpy(dotdot->name, "..");
  dotdot->index = dir_table;
  dotdot->size = 0;
  dotdot->type = FAT_DIR;
  dotdot->epoch = (uint32_t) time(NULL);

  return RSH_OK;

}

/*
 * This is what will be called by the file system manager. We can assume good
 * data.
 */
int rsh_fat16_mkdir(const char *path){

  int err;
  char *copy;
  char *dir, *name;
  struct rsh_fat_dirent ent;
  
  copy = strdup(path);
  /* If the last character is a '/', nuke it. */
  if ( copy[strlen(copy)-1] == '/' )
    copy[strlen(copy)-1] = 0;

  dir = _rsh_fat16_split_path(copy, &name);
  printf("parent: %s, new dir: %s\n", dir, name);

  /* New directory is in the root directory. */
  if ( ! *dir ){
    /* Fill in ent enough to point to the root directory table. */
    ent.index = fat16_fs.fs_header.root_offset;
  } else {
    err = _rsh_fat16_path_to_dirent(dir, &ent, NULL);
    if ( err )
      return err;
  }

  return _rsh_fat16_mkdir(ent.index, name);

}

/*
 * Make a file entry in a directory table.
 */
int _rsh_fat16_mkfile(uint32_t dir_table, const char *name){

  int err;
  uint32_t tail;
  uint32_t cluster;
  uint32_t file_cluster;
  struct rsh_fat_dirent *slot;

  /* First we need to make sure there is room for another dirent. */
  slot = _rsh_fat16_find_open_dirent(dir_table);
  if ( ! slot ){
    err = _rsh_fat16_find_open_cluster(&cluster);
    if ( err < 0 ){
      errno = ENOSPC;
      return RSH_ERR;
    }
    /* Tack this cluster onto the dir table. */
    tail = _rsh_fat16_follow_head(dir_table, -1);
    rsh_fat16_set_entry(tail, cluster);
    rsh_fat16_set_entry(cluster, FAT_TERM);
    slot = _rsh_fat16_find_open_dirent(dir_table);
  }

  /* Now we have a slot. Get a cluster for the file. */
  err = _rsh_fat16_find_open_cluster(&file_cluster);
  if ( err < 0 ){
    errno = ENOSPC;
    return RSH_ERR;
  }
  rsh_fat16_set_entry(file_cluster, FAT_TERM);
  printf("Setting file cluster FAT entry (%u) to FAT_TERM\n", file_cluster);

  /* Fill in the file's entry. */
  strncpy(slot->name, name, 112);
  slot->name[111] = 0; /* Make sure its null terminated. */
  slot->index = file_cluster;
  slot->size = 0;
  slot->type = FAT_FILE;
  slot->epoch = (uint32_t) time(NULL);

  return RSH_OK;

}

/*
 * Get and return the value of an entry in the FAT table. This is a convience
 * wrapper for calculating the proper offsets and the like for a multicluster
 * FAT.
 */
fat_t rsh_fat16_get_entry(uint32_t index){

  uint32_t fat_cluster;
  uint32_t fat_offset;
  uint32_t fat_real_cluster;
  fat_t *fat_section;

  if ( index > fat16_fs.fat_entries )
    return FAT_RESERVED;

  fat_cluster = index / fat16_fs.fat_per_cluster;
  fat_offset = index % fat16_fs.fat_per_cluster;

  /* Get the cluster address for the particular cluster we are referencing. 
   * Our little secret: that address can be found pretty easily since by design
   * the FAT table will be made up of contiguous clusters.
   */
  fat_real_cluster = fat16_fs.fs_header.fat_offset + fat_cluster;
  fat_section = FAT_CLUSTER_TO_ADDR(fat_real_cluster);

  /* Now we can address the fat entry via the fat_section pointer. */
  return fat_section[fat_offset];

}

/*
 * Set the entry pointed to by index to the value contained in value. This is
 * a convience function to properly calculate offsets for multicluster FATs.
 */
void rsh_fat16_set_entry(uint32_t index, fat_t value){

  uint32_t fat_cluster;
  uint32_t fat_offset;
  uint32_t fat_real_cluster;
  fat_t *fat_section;

  if ( index > fat16_fs.fat_entries )
    return;

  /* First figure out what cluster the index is in, and what offset the
   * particular entry is in. */
  fat_cluster = index / fat16_fs.fat_per_cluster;
  fat_offset = index % fat16_fs.fat_per_cluster;

  /* Now locate the actual cluster that we are referencing. */
  fat_real_cluster = fat16_fs.fs_header.fat_offset + fat_cluster;
  fat_section = FAT_CLUSTER_TO_ADDR(fat_real_cluster);

  fat_section[fat_offset] = value;

}

/*
 * Make a new file system.
 */
int _rsh_fat16_init_creat(char *path, struct rsh_fat16_fs *fs, 
			  size_t size, size_t cluster){

  int i;
  int flags = O_CREAT|O_RDWR;
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  uint32_t start;
  struct rsh_fat_dirent *dot;
  struct rsh_fat_dirent *dotdot;

  if ( sizeof(struct rsh_fat_dirent) * 3 > cluster ){
    printf("Cluster size too small.\n");
    return RSH_ERR;
  }

  /* First things first, map the memory we need into a file. */
  fs->fs_fd = open(path, flags, mode);
  if ( fat16_fs.fs_fd < 0 ){
    perror("open");
    return RSH_ERR;
  }

  if ( lseek(fs->fs_fd, size-1, SEEK_SET) == -1 ){
    perror("lseek");
    return RSH_ERR;
  }

  if ( write(fs->fs_fd, "", 1) != 1){
    perror("write");
    return RSH_ERR;
  }

  fs->fs_io = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED,
			fs->fs_fd, 0);
  if ( fs->fs_io == MAP_FAILED ){
    perror("mmap");
    return RSH_ERR;
  }

  /* Now deal with some of the FS mechanics. */
  fs->fs_header.csize = cluster;
  fs->fs_header.len = size;
  fs->fs_header.root_offset = 1;
  fs->fs_header.fat_offset = 2;

  /* Figure out how big the FAT needs to be in clusters: size/cluster will give
   * the number of required clusters. */
  fs->fat_entries = size / cluster;
  fs->fat_clusters = (fs->fat_entries * sizeof(fat_t)) / cluster;

  /* Deal with rounding issues. */
  if ( (fs->fat_clusters * cluster) < (fs->fat_entries * sizeof(fat_t)) )
    fs->fat_clusters++;

  fs->fat_per_cluster = cluster / sizeof(fat_t);
  fs->fat_size = fs->fat_entries * sizeof(fat_t);

  fs->dir_per_cluster = cluster / sizeof(struct rsh_fat_dirent);

  /* At this point we know: where the root directory table starts and ends
   * and where the FAT table starts. From here we need to make the FAT.
   */
  start = fs->fs_header.fat_offset;
  for ( i = 0; i < fs->fat_clusters-1; i++){
    rsh_fat16_set_entry(start, start+1);
    start++;
  }
  rsh_fat16_set_entry(start, FAT_TERM);

  /* Mark the root_dir entry and header entry in the FAT. */
  rsh_fat16_set_entry(0, FAT_TERM);
  rsh_fat16_set_entry(1, FAT_TERM);

  /* And create the first two directory entries we need: . and .. */
  dot = FAT_CLUSTER_TO_ADDR(fs->fs_header.root_offset);
  dotdot = FAT_CLUSTER_TO_ADDR(fs->fs_header.root_offset) + 
    sizeof(struct rsh_fat_dirent);

  /* dot points to ourselves. */
  strcpy(dot->name, ".");
  dot->index = fs->fs_header.root_offset;
  dot->size = 0;
  dot->type = FAT_DIR;
  dot->epoch = (uint32_t) time(NULL);

  /* dotdot points to our parent, but in this case is just ourself. */
  strcpy(dotdot->name, "..");
  dotdot->index = fs->fs_header.root_offset;
  dotdot->size = 0;
  dotdot->type = FAT_DIR;
  dotdot->epoch = (uint32_t) time(NULL);

  /* Finally copy the data structures we generated into the actual file system
   * data. */
  memcpy(fat16_fs.fs_io, &(fat16_fs.fs_header), sizeof(struct rsh_fs_block));
  msync(fat16_fs.fs_io, fat16_fs.fs_header.len, MS_SYNC);

  /* At this point we should be good. */
  return 0;

}

/*
 * Open a previously created FAT16 filesystem. Ugh this one is actually more
 * annoying that creating a new file system. Nvm, I take that back.
 */
int _rsh_fat16_init_open(char *path, struct rsh_fat16_fs *fs){

  int flags = O_CREAT|O_RDWR;
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  ssize_t bytes;

  /* First things first, we must open the filesystem requested and fill in
   * the file system header information. */
  fs->fs_fd = open(path, flags, mode);
  if ( fs->fs_fd < 0 ){
    perror("open");
    return RSH_ERR;
  }

  /* Now read out the header. */
  bytes = read(fs->fs_fd, &(fs->fs_header), sizeof(struct rsh_fs_block));
  if ( bytes != sizeof(struct rsh_fs_block)){
    perror("read");
    return RSH_ERR;
  }

  /* We should have the header, so map in the entirety of the file system
   * now. */
  if ( lseek(fs->fs_fd, fs->fs_header.len, SEEK_SET) < 0 ){
    perror("lseek");
    return RSH_ERR;
  }
  fs->fs_io = mmap(NULL, fs->fs_header.len, PROT_READ|PROT_WRITE, 
		   MAP_SHARED, fs->fs_fd, 0);
  if ( fs->fs_io == MAP_FAILED ){
    perror("mmap");
    return RSH_ERR;
  }
  
  /* Now populate the rest of the rsh_fat16_fs struct we were passed. */
  fs->fat_entries = fs->fs_header.len / fs->fs_header.csize;
  fs->fat_clusters = (fs->fat_entries * sizeof(fat_t)) / fs->fs_header.csize;
  if ( (fs->fat_clusters * fs->fs_header.csize) < 
       (fs->fat_entries * sizeof(fat_t)) ){
    fs->fat_clusters++;
  }
  fs->fat_per_cluster = fs->fs_header.csize / sizeof(fat_t);
  fs->fat_size = fs->fat_entries * sizeof(fat_t);

  /* OK, and we are done. */
  return RSH_OK;

}

struct rsh_io_ops fops = {

  .read =  rsh_fat16_read,
  .write = rsh_fat16_write,
  .open =  rsh_fat16_open,
  .close = rsh_fat16_close,
  .readdir = rsh_fat16_readdir,

};

/*
 * Load up a file system or create a file system. The passed path is the
 * path to the local file on disk. We will memory map this into our process'
 * address space for ease of access. This is pretty legit. And apparently
 * is portable to Solaris, too. Which is a real shocker since Solaris is a
 * serious peice of shit. But eh, sometimes life can be good, right? 
 */ 
int rsh_fat16_init(char *local_path, size_t size, size_t cluster){

  int err;
  struct stat buf;

  if ( stat(local_path, &buf) )
    err = _rsh_fat16_init_creat(local_path, &fat16_fs, size, cluster);
  else
    err = _rsh_fat16_init_open(local_path, &fat16_fs);

  printf(" FAT16 has been loaded.\n");

  if ( err )
    return err;

  /* Now register the file system driver (us) so that we can actually do
   * stuff. */
  rsh_register_fs(&fops, local_path, &fat16_fs);

  return RSH_OK;

}

/*
 * A builtin command to diplay our FAT16 info. Mostly for debugging and stuff.
 */
int builtin_fatinfo(int argc, char **argv, int in, int out, int err){

  printf("FAT16 Header:\n");
  printf("  csize:           %d\n", fat16_fs.fs_header.csize);
  printf("  length:          %d\n", fat16_fs.fs_header.len);
  printf("  root_offset:     %d\n", fat16_fs.fs_header.root_offset);
  printf("  fat_offset:      %d\n", fat16_fs.fs_header.fat_offset);
  printf("Internal info:\n");
  printf("  fat_entries:     %d\n", fat16_fs.fat_entries);
  printf("  fat_per_cluster: %d\n", fat16_fs.fat_per_cluster);
  printf("  fat_clusters:    %d\n", fat16_fs.fat_clusters);
  printf("  fat_size:        %d\n", fat16_fs.fat_size);
  printf("Memory map address: 0x%016lx\n", (long unsigned int)fat16_fs.fs_io);

  return 0;

}

/*
 * A debugging function to display the FAT. This is only a good idea for small
 * file systems...
 */
void _rsh_fat16_display_fat(){

  uint32_t i;
  uint32_t fat_offset = fat16_fs.fs_header.fat_offset;
  
  for ( i = 0; i < fat16_fs.fat_entries; i++){

    if ( i % fat16_fs.fat_per_cluster == 0 )
      printf("FAT Cluster: %d\n", fat_offset);

    printf("  %5d:   0x%04x\n", i, rsh_fat16_get_entry(i));

  }

}

void _rsh_fat16_display_dir(uint32_t dir){

  int i;
  int index = 0;
  uint32_t clust = dir;
  struct rsh_fat_dirent *dir_entries;

  do {
  start:

    /* Search the cluster. */
    dir_entries = FAT_CLUSTER_TO_ADDR(clust);
    for ( i = 0; i < fat16_fs.dir_per_cluster; i++){

      if ( *(dir_entries[i].name) ){
	printf("Entry %d\n", index++);
	printf(" name  %s\n", dir_entries[i].name);
	printf(" index %u\n", dir_entries[i].index);
	printf(" size  %u\n", dir_entries[i].size);
	printf(" type  0x%02x\n", dir_entries[i].type);
	printf(" epoch %u\n", dir_entries[i].epoch);
      }

    }
    
    /* And increment the clust address (if necessary). */
    if ( rsh_fat16_get_entry(clust) != FAT_TERM ){
      clust = rsh_fat16_get_entry(clust);
      goto start;
    }

  } while ( rsh_fat16_get_entry(clust) != FAT_TERM );

}

void _rsh_fat16_display_root_dir(){

  _rsh_fat16_display_dir(fat16_fs.fs_header.root_offset);

}

void _rsh_fat16_display_path_dir(const char *path){

  int err;
  struct rsh_fat_dirent ent;
  
  err = _rsh_fat16_path_to_dirent(path, &ent, NULL);
  if ( err ){
    perror("display dir");
    return;
  }

  if ( ent.type != FAT_DIR ){
    printf("%s is not a directory.\n", path);
    return;
  }

  _rsh_fat16_display_dir(ent.index);

}
