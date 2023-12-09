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

#define ROOT_DIR_NAME "/"
#define DIR_DELIM_STR "/"
#define DIR_DELIM_CHR '/'

#define SUPERBLOCK_BLK 0
#define ROOT_INODE_BLK 1
#define ROOT_INFO_BLK 2
#define FREE_LIST_BLK 3

/****************************************************************************
 * auxiliar functions
 ***************************************************************************/

size_t fs_inode_max_links(struct superblock *sb) {
  return (sb->blksz - sizeof(struct inode)) / (sizeof(uint64_t));
}

size_t fs_nodeinfo_max_name_size(struct superblock *sb) {
  return (sb->blksz - sizeof(struct nodeinfo)) / (sizeof(char));
}

int fs_write_blk(struct superblock *sb, uint64_t pos, void *data) {
  if (lseek(sb->fd, pos * sb->blksz, SEEK_SET) == -1) 
    return -1;
  
  if (write(sb->fd, data, sb->blksz) == -1) 
    return -1;
  
  return 0;
}

int fs_read_blk(struct superblock *sb, uint64_t pos, void *buf) {
  if (lseek(sb->fd, pos * sb->blksz, SEEK_SET) == -1) 
    return -1;
  
  if (read(sb->fd, buf, sb->blksz) == -1) 
    return -1;
  
  return 0;
}

int fs_is_absolute_path(const char *path) {
  return strncmp(path, ROOT_DIR_NAME, strlen(ROOT_DIR_NAME)) == 0;
}

char * fs_get_basedir(const char *path) {
  int n = (int)(strrchr(path, DIR_DELIM_CHR) - path);

  char *basedir = (char*) malloc((n + 2) * sizeof(char));
  *basedir = '\0';

  if (n == 0) {
    strcat(basedir, "/");
  } else {
    strncat(basedir, path, n);
  }

  return basedir;
}

char * fs_get_basename(const char *path) {
  char *c = strrchr(path, DIR_DELIM_CHR);

  char *basename = (char*) malloc(strlen(c) * sizeof(char));
  strcpy(basename, c+1);

  return basename;
}

uint64_t fs_find_blk(struct superblock *sb, const char *name) {
  if (!fs_is_absolute_path(name)) {
    errno = ENOENT;
    return (uint64_t) -1;
  }

  if (strlen(name) == 1) {
    return sb->root;
  }

  char *name_c = (char*) malloc((strlen(name) + 1) * sizeof(char));
  strcpy(name_c, name);

  struct inode* inode = (struct inode*) malloc(sb->blksz);
  struct nodeinfo* nodeinfo = (struct nodeinfo*) malloc(sb->blksz);

  fs_read_blk(sb, ROOT_INODE_BLK, (void*) inode);
  fs_read_blk(sb, inode->meta, (void*) nodeinfo);

  struct inode* child_inode = (struct inode*) malloc(sb->blksz);
  struct nodeinfo* child_nodeinfo = (struct nodeinfo*) malloc(sb->blksz);

  uint64_t blk_pos = ROOT_INODE_BLK;

  char *token = strtok(name_c, DIR_DELIM_STR);

  while (token != NULL) {
    int found = 0;

    for (int i=0; i<nodeinfo->size; i++) {
      fs_read_blk(sb, inode->links[i], (void*)child_inode);

      fs_read_blk(sb, child_inode->meta, (void*)child_nodeinfo);

      if (strcmp(child_nodeinfo->name, token) == 0) {
        found = 1;
        blk_pos = inode->links[i];        
        break;
      }
    }

    if (found != 1) {
      errno = ENOENT;
      blk_pos = (uint64_t) -1;
      break;
    }

    token = strtok(NULL, DIR_DELIM_STR);

    if (token == NULL) {
      break;
    }

    if (child_inode->mode != IMDIR) {
      errno = ENOTDIR;
      blk_pos = (uint64_t) -1;
      break;
    }

    memcpy(inode, child_inode, sb->blksz);
    memcpy(nodeinfo, child_nodeinfo, sb->blksz);
  }

  free(name_c);
  free(inode);
  free(nodeinfo);
  free(child_inode);
  free(child_nodeinfo);

  return blk_pos;
}

int fs_link_blk(struct superblock *sb, uint64_t parent_blk, uint64_t link_blk) {
  struct inode *inode = (struct inode*) malloc(sb->blksz);
  fs_read_blk(sb, parent_blk, (void*) inode);

  if (inode->mode != IMDIR) {
    free(inode);
    errno = ENOTDIR;
    return -1;
  }
  
  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  fs_read_blk(sb, inode->meta, (void*) nodeinfo);

  size_t max_links = fs_inode_max_links(sb);

  if (nodeinfo->size == max_links) {
    free(inode);
    free(nodeinfo);
    errno = EMLINK;
    return -1;
  }

  for (int i=0; i<max_links; i++) {
    if (inode->links[i] == (uint64_t) -1) {
      inode->links[i] = link_blk;
      nodeinfo->size++;
      break;
    }
  }

  fs_write_blk(sb, parent_blk, (void*) inode);
  fs_write_blk(sb, inode->meta, (void*) nodeinfo);

  free(inode);
  free(nodeinfo);

  return 0;
}

int fs_unlink_blk(struct superblock *sb, uint64_t parent_blk, uint64_t link_blk) {
  struct inode *inode = (struct inode*) malloc(sb->blksz);
  fs_read_blk(sb, parent_blk, (void*) inode);

  if (inode->mode != IMDIR) {
    free(inode);
    errno = ENOTDIR;
    return -1;
  }

  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  fs_read_blk(sb, inode->meta, (void*) nodeinfo);

  for (int i=0; i<nodeinfo->size; i++) {
    if (inode->links[i] == link_blk) {
      inode->links[i] = (uint64_t) -1;
      nodeinfo->size--;
      break;
    }
  }

  fs_write_blk(sb, parent_blk, (void*) inode);
  fs_write_blk(sb, inode->meta, (void*) nodeinfo);

  free(inode);
  free(nodeinfo);

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

  for (int i=0; i<fs_inode_max_links(sb); i++) {
    root_inode->links[i] = (uint64_t) -1;
  }

  if (fs_write_blk(sb, ROOT_INODE_BLK, (void*) root_inode) == -1)
    return NULL;

  free(root_inode);

  // ----- Root node info -----

  struct nodeinfo* root_info = (struct nodeinfo*) malloc(blocksize);

  if (root_info == NULL) {
    return NULL;
  }

  strcpy(root_info->name, ROOT_DIR_NAME);
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
  return 0;
}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz) {
  return 0;
}

int fs_unlink(struct superblock *sb, const char *fname) {
  return 0;
}

int fs_mkdir(struct superblock *sb, const char *dname) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  if (strlen(dname) == 0 || !fs_is_absolute_path(dname) || strchr(dname, ' ') != NULL) {
    errno = ENOTDIR;
    return -1;
  }

  if (sb->freeblks < 2) {
    errno = EBUSY;
    return -1;
  }

  if (fs_find_blk(sb, dname) != (uint64_t) -1) {
    errno = EEXIST;
    return -1;
  }

  char *name = fs_get_basename(dname);

  if (strlen(name) > fs_nodeinfo_max_name_size(sb)) {
    free(name);
    errno = ENAMETOOLONG;
    return -1;
  }

  char *parent_name = fs_get_basedir(dname);

  uint64_t parent_blk = fs_find_blk(sb, parent_name);
  free(parent_name);

  if (parent_blk == (uint64_t) -1) {
    free(name);
    errno = ENOTDIR;
    return -1;
  }

  uint64_t inode_blk = fs_get_block(sb);
  uint64_t nodeinfo_blk = fs_get_block(sb);

  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  nodeinfo->size = 0;
  strcpy(nodeinfo->name, name);

  free(name);

  struct inode *inode = (struct inode*) malloc(sb->blksz);
  inode->mode = IMDIR;
  inode->next = 0;
  inode->parent = parent_blk;
  inode->meta = nodeinfo_blk;

  for (int i=0; i<fs_inode_max_links(sb); i++) {
    inode->links[i] = (uint64_t) -1;
  }
  
  fs_write_blk(sb, inode_blk, (void *) inode);
  fs_write_blk(sb, nodeinfo_blk, (void *) nodeinfo);

  fs_link_blk(sb, parent_blk, inode_blk);

  free(inode);
  free(nodeinfo);

  return 0;
}

int fs_rmdir(struct superblock *sb, const char *dname) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  uint64_t blk = fs_find_blk(sb, dname);

  if (blk == (uint64_t) -1) {
    errno = EEXIST;
    return -1;
  }

  struct inode *inode = (struct inode*) malloc(sb->blksz);
  fs_read_blk(sb, blk, (void*) inode);
  
  if (inode->mode != IMDIR) {
    free(inode);
    errno = ENOTDIR;
    return -1;
  }
  
  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  fs_read_blk(sb, inode->meta, (void*) nodeinfo);

  if (nodeinfo->size > 0) {
    free(inode);
    free(nodeinfo);
    errno = ENOTEMPTY;
    return -1;
  }

  fs_unlink_blk(sb, inode->parent, blk);

  fs_put_block(sb, inode->meta);
  fs_put_block(sb, blk);

  free(inode);
  free(nodeinfo);

  return 0;
}

char * fs_list_dir(struct superblock *sb, const char *dname) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return NULL;
  }

  uint64_t blk = fs_find_blk(sb, dname);

  if (blk == (uint64_t) -1) {
    errno = EEXIST;
    return NULL;
  }

  struct inode *inode = (struct inode*) malloc(sb->blksz);
  fs_read_blk(sb, blk, (void*) inode);
  
  if (inode->mode != IMDIR) {
    free(inode);
    errno = ENOTDIR;
    return NULL;
  }
  
  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  fs_read_blk(sb, inode->meta, (void*) nodeinfo);

  struct inode *link_inode = (struct inode*) malloc(sb->blksz);
  struct nodeinfo *link_nodeinfo = (struct nodeinfo*) malloc(sb->blksz);

  char *result = (char*) malloc(100 * sizeof(char));
  *result = '\0';

  for (int i=0; i<nodeinfo->size; i++) {
    fs_read_blk(sb, inode->links[i], (void*) link_inode);
    fs_read_blk(sb, link_inode->meta, (void*) link_nodeinfo);

    strcat(result, link_nodeinfo->name);

    if (link_inode->mode == IMDIR) {
      strcat(result, DIR_DELIM_STR);
    }

    if (i < nodeinfo->size - 1) {
      strcat(result, " ");
    }
  }

  free(inode);
  free(nodeinfo);
  free(link_inode);
  free(link_nodeinfo);

  return result;
}