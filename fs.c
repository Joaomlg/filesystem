#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "fs.h"

#define SUPERBLOCK_MAGIC 0xdcc605f5
#define SUPERBLOCK_BLK 0
#define ROOT_INODE_BLK 1
#define ROOT_INFO_BLK 2
#define FREE_LIST_BLK 3

/****************************************************************************
 * auxiliar functions
 ***************************************************************************/

int fs_write_blk(struct superblock* sb, uint64_t pos, void *data) {
  if (lseek(sb->fd, pos * sb->blksz, SEEK_SET) == -1) 
    return -1;
  
  if (write(sb->fd, data, sb->blksz) == -1) 
    return -1;
  
  return 0;
}

int fs_read_blk(struct superblock* sb, uint64_t pos, void *buf) {
  if (lseek(sb->fd, pos * sb->blksz, SEEK_SET) == -1) 
    return -1;
  
  if (read(sb->fd, buf, sb->blksz) == -1) 
    return -1;
  
  return 0;
}

/****************************************************************************
 * external functions
 ***************************************************************************/

struct superblock * fs_format(const char *fname, uint64_t blocksize) {
  if (blocksize < MIN_BLOCK_SIZE) {
    errno = EINVAL;
    return NULL;
  }

  int fd = open(fname, O_RDWR, S_IRUSR | S_IWUSR);

  off_t fsize = lseek(fd, 0, SEEK_END);

  long nblocks = fsize / blocksize;

  if (nblocks < MIN_BLOCK_COUNT) {
    errno = ENOSPC;
    return NULL;
  }

  if (flock(fd, LOCK_EX) == -1) {
    close(fd);
    errno = EBUSY;
    return NULL;
  }

  lseek(fd, 0, SEEK_SET);

  // ----- Superblock -----

  struct superblock *sb = (struct superblock*) malloc(sizeof(struct superblock));

  if (sb == NULL) {
    return NULL;
  }
  
  sb->magic = SUPERBLOCK_MAGIC;
  sb->blksz = blocksize;
  sb->blks = nblocks;
  sb->freeblks = nblocks - 3;
  sb->root = ROOT_INODE_BLK;
  sb->freelist = FREE_LIST_BLK;
  sb->fd = fd;

  if (fs_write_blk(sb, SUPERBLOCK_BLK, (void*) sb) == -1) 
    return NULL;

  // ----- Root inode -----

  struct inode* root_inode  = (struct inode*) malloc(blocksize);
  
  root_inode->mode = IMDIR;
  root_inode->parent = SUPERBLOCK_BLK;
  root_inode->meta = ROOT_INFO_BLK;
  root_inode->next = 0;

  if (fs_write_blk(sb, ROOT_INODE_BLK, (void*) root_inode) == -1)
    return NULL;

  free(root_inode);

  // ----- Root node info -----

  struct nodeinfo* root_info = (struct nodeinfo*) malloc(blocksize);

  if (root_info == NULL) {
    return NULL;
  }

  strcpy(root_info->name, "/");
  root_info->size = 0;
  
  if (fs_write_blk(sb, ROOT_INFO_BLK, (void*) root_info) == -1)
    return NULL;

  free(root_info);

  // ----- Free list -----

  struct freepage *freepage = (struct freepage*) malloc(blocksize);

  freepage->count = 0;

  for (int i=sb->freelist; i<sb->blks; i++) {
    freepage->next = (i == sb->blks - 1) ? 0 : i + 1;
    fs_write_blk(sb, i, (void*) freepage);
  }

  free(freepage);

  // ----- End -----
  
  return sb;
}

struct superblock * fs_open(const char *fname) {
  int fd = open(fname, O_RDWR, S_IRUSR | S_IWUSR);

  if (fd == -1) {
    return NULL;
  }

  if (flock(fd, LOCK_EX) == -1) {
    close(fd);
    errno = EBUSY;
    return NULL;
  }

  struct superblock* sb = (struct superblock*) malloc(sizeof(struct superblock));

  if (sb == NULL) {
    flock(fd, LOCK_UN);
    close(fd);
    return NULL;
  }

  if (read(fd, (void*) sb, sizeof(struct superblock)) == -1) {
    flock(fd, LOCK_UN);
    close(fd);
    free(sb);
    return NULL;
  }

  if (sb->magic != SUPERBLOCK_MAGIC) {
    flock(fd, LOCK_UN);
    close(fd);
    free(sb);
    errno = EBADF;
    return NULL;
  }

  sb->fd = fd;

  return sb;
}

int fs_close(struct superblock *sb) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  flock(sb->fd, LOCK_UN);
  close(sb->fd);
  
  free(sb);

  return 0;
}

uint64_t fs_get_block(struct superblock *sb) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return (uint64_t) -1;
  }

  if (sb->freeblks == 0) 
    return 0;

  struct freepage* freepage = (struct freepage*) malloc(sb->blksz);

  if (freepage == NULL) 
    return (uint64_t) -1;

  if (fs_read_blk(sb, sb->freelist, (void *) freepage) == -1) {
    free(freepage);
    return (uint64_t) -1;
  }

  uint64_t block = sb->freelist;

  sb->freeblks--;
  sb->freelist = freepage->next;

  if (fs_write_blk(sb, SUPERBLOCK_BLK, (void *) sb) == -1) {
    free(freepage);
    return (uint64_t) -1;
  }

  free(freepage);

  return block;
}

int fs_put_block(struct superblock *sb, uint64_t block) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  struct freepage* freepage = (struct freepage*) malloc(sb->blksz);

  if (freepage == NULL) 
    return -1;

  freepage->count = 0;
  freepage->next = sb->freelist;

  sb->freelist = block;
  sb->freeblks++;

  if (fs_write_blk(sb, block, (void *) freepage) == -1 \
  || fs_write_blk(sb, SUPERBLOCK_BLK, (void *) sb) == -1) {
    free(freepage);
    return -1;
  }

  free(freepage);

  return 0;
}

int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt) {

}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz) {

}

int fs_unlink(struct superblock *sb, const char *fname) {

}

int fs_mkdir(struct superblock *sb, const char *dname) {

}

int fs_rmdir(struct superblock *sb, const char *dname) {

}

char * fs_list_dir(struct superblock *sb, const char *dname) {

}