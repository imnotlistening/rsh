/*
 * RSH file system implementation. Yikes. Basically a FAT16 FS under the hood.
 * The useful syscalls that are used for a normal file system are implemented
 * in userspace for us. These "sys" calls are: read(), write(), dup2(), open(),
 * close(). 
 */

#include <rsh.h>

#include <stdint.h>
#include <sys/types.h>

/* These are the under the hood operations for the file system. */
ssize_t _rsh_read(int fd, void *buf, size_t count);
ssize_t _rsh_write(int fd, const void *buf, size_t count);
int     _rsh_dup2(int oldfd, int newfd);
int     _rsh_open(const char *pathname, int flags, mode_t mode);
int     _rsh_close(int fd);

struct rsh_file;

/* A definition for a file systems I/O operations. */
struct rsh_io_ops {

  ssize_t (*read)(struct rsh_file *file, void *buf, size_t count);
  ssize_t (*write)(struct rsh_file *file, const void *buf, size_t count);
  int     (*open)(struct rsh_file *file, const char *pathname);
  int     (*close)(struct rsh_file *file);

};

/*
 * A structure for representing a file.
 */
struct rsh_file {

  /* Set to zero if this file structure is not in use. */
  int used;

  /* Reference count. If this hits 0, close this file. */
  int references;

  /* Some info about the file. */
  int size;
  int modification;

  /* Offset into the file, self explanitory. */
  off_t offset;

  /* Store the path of the file. */
  const char *path;

  /* The operations structure so we know how to actually use the file. */
  struct rsh_io_ops *fops;

  /* A pointer into the underlying file systems data to describe the file. */
  void *local;

};

/*
 * A structure for describing a directory.
 */
struct rsh_dirent {

  /* This one is simple... */
  char *name;
  
  /* Umm */

};

/*
 * The actual file system. This is for the RSH internal files not for
 * native files. That is handled by the underlying OS.
 */
struct rsh_file_system {

  /* Pointer to the file table. */
  struct rsh_file *ftable;

  /* Book keeping. */
  int ft_length;
  int used;

  /* fops struct so that we know how to to open a file. */
  struct rsh_io_ops *fops;

  /* Pointer to driver specific data. */
  void *driver;

};

/* Some high level functions for RSH to call. */
int rsh_init_fs();
int rsh_register_fs(struct rsh_io_ops *fops, char *name, void *driver);
int rsh_unregister_fs();

/*
 * These are the defines for the default built in file system: a FAT16. Ugh.
 * Some day I may implement an EXT based system for the shell but that is for
 * another day.
 */

typedef uint32_t fat_t; /* This really should be 16 bits, but oh well. */

/* FAT special values. */
#define FAT_FREE      0x00000000
#define FAT_RESERVED  0x0000fffe
#define FAT_TERM      0x0000ffff

/* File entry types. */
#define FAT_FILE      0x00
#define FAT_DIR       0xff

/*
 * Boot record. Lol. Well anyway, as defined by the specs, but a little extra
 * spice for some fun. Maybe.
 */
struct rsh_fs_block {

  uint32_t csize;
  uint32_t len;
  uint32_t root_offset;
  uint32_t fat_offset;

} __attribute__((packed));

/*
 * A struct to describe more than just the layout of the FAT file system.
 */
struct rsh_fat16_fs {

  /* The header for the filesystem. Read out of the FS itself. */
  struct rsh_fs_block fs_header;

  /* Fat related stuff. */
  uint32_t fat_entries;      /* Number of entries in the FAT. */
  uint32_t fat_per_cluster;  /* Number of fat_t's that fit into a cluster. */
  uint32_t fat_clusters;     /* Number of clusters required to store FAT. */
  uint32_t fat_size;         /* Size of the FAT in bytes. */
  
  /* The starting address of the file systems mmap()'ed data. */
  void *fs_io;

  /* The file descriptor for the native file. */
  int fs_fd;

};

/*
 * A directory entry. Not the same thing that the FS deals with though...
 */
struct rsh_fat_dirent {

  char name[112];
  uint32_t index;
  uint32_t size;
  uint32_t type;
  uint32_t epoch; /* Epoch is just a cool word. Yup. */

} __attribute__((packed));

/* And finally some functions. */
int   rsh_fat16_init(char *local_path, size_t size, size_t cluster);
fat_t rsh_fat16_alloc_cluster(fat_t parent);

/* These are intended for internal and/or debugging use. */
fat_t _rsh_fat16_find_open_cluster();
fat_t _rsh_fat16_get_entry(uint32_t index);
void  _rsh_fat16_set_entry(uint32_t index, fat_t value);
void  _rsh_fat16_display_fat();
