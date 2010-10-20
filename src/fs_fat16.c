/*
 * A FAT16 implementation that can be utilised by RSH. Oh well.
 */

#include <rsh.h>
#include <rshfs.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

struct rsh_fat16_fs fat16_fs;

#define FAT_CLUSTER_TO_ADDR(cluster)		\
  ( fat16_fs.fs_io + (cluster * fat16_fs.fs_header.csize) )

/*
 * This is a *bad* function. It should only be called when there is irrevocable
 * corruption to the file system. Tihs will cause a slow and painful death for
 * the shell: AAARrrghhh.
 */
void rsh_fat16_corrupt(){

  printf("FAT16 filesystem irrevocably corrupt. Cry moar.\n");
  exit(1);

}

/*
 * Trace a file through the FAT. Head is the starting location, offset is the
 * maximum allowed jumps. If the particular file in the FAT ends before offset
 * jumps, the index of the last fat accessed is returned. Otherwise, the index
 * 'offset' jumps from head is returned.
 */
inline fat_t _rsh_fat16_follow_head(uint32_t head, uint32_t offset){

  fat_t fat_index;
  fat_t next_index;

  fat_index = head;
  while ( offset-- > 0 ){

    next_index = _rsh_fat16_get_entry(fat_index);
    
    if ( next_index == FAT_TERM )
      break;

    /* This is bad. */
    if ( next_index == FAT_RESERVED || next_index == FAT_FREE )
      rsh_fat16_corrupt();

    fat_index = next_index;

  }

  return fat_index;

}

ssize_t _rsh_fat16_read(struct rsh_file *file, void *buf, size_t count){

  return 0;

}

ssize_t _rsh_fat16_write(struct rsh_file *file, const void *buf, size_t count){

  

  return 0;

}

int _rsh_fat16_open(struct rsh_file *file, const char *pathname){

  return 0;

}

int _rsh_fat16_close(struct rsh_file *file){

  return 0;

}

/*
 * Get and return the value of an entry in the FAT table. This is a convience
 * wrapper for calculating the proper offsets and the like for a multicluster
 * FAT.
 */
fat_t _rsh_fat16_get_entry(uint32_t index){

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
void _rsh_fat16_set_entry(uint32_t index, fat_t value){

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

  /* At this point we know: where the root directory table starts and ends
   * and where the FAT table starts. From here we need to make the FAT.
   */
  start = fs->fs_header.fat_offset;
  for ( i = 0; i < fs->fat_clusters-1; i++){
    _rsh_fat16_set_entry(start, start+1);
    start++;
  }
  _rsh_fat16_set_entry(start, FAT_TERM);

  /* Mark the root_dir entry and header entry in the FAT. */
  _rsh_fat16_set_entry(0, FAT_TERM);
  _rsh_fat16_set_entry(1, FAT_TERM);

  /* Finally copy the data structures we generated into the actual file system
   * data. */
  memcpy(fat16_fs.fs_io, &(fat16_fs.fs_header), sizeof(struct rsh_fs_block));
  msync(fat16_fs.fs_io, fat16_fs.fs_header.len, MS_SYNC);

  /* At this point we should be good. */
  return 0;

}

/*
 * Open a previously created FAT16 filesystem. Ugh this one is actually more
 * annoying that creating a new file system.
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

  .read = _rsh_fat16_read,
  .write = _rsh_fat16_write,
  .open = _rsh_fat16_open,
  .close = _rsh_fat16_close,

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

    printf("  %5d:   0x%04x\n", i, _rsh_fat16_get_entry(i));

  }

}

