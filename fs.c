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

#define INVALID_BLOCK ((uint64_t) -1)

/****************************************************************************
 * auxiliar functions
 ***************************************************************************/
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define CEIL(X, Y) (((X) / (Y)) + (((X) % (Y) > 0) ? 1 : 0))

uint64_t fs_inode_max_links(struct superblock *sb) {
  return (sb->blksz - sizeof(struct inode)) / (sizeof(uint64_t));
}

uint64_t fs_nodeinfo_max_name_size(struct superblock *sb) {
  return (sb->blksz - sizeof(struct nodeinfo)) / (sizeof(char));
}

int fs_write_blk_sz(struct superblock *sb, uint64_t pos, void *data, size_t sz) {
  if (lseek(sb->fd, pos * sb->blksz, SEEK_SET) == -1) 
    return -1;
  
  if (write(sb->fd, data, sz) == -1) 
    return -1;
  
  return 0;
}

int fs_write_blk(struct superblock *sb, uint64_t pos, void *data) {
  return fs_write_blk_sz(sb, pos, data, sb->blksz);
}

int fs_read_blk_sz(struct superblock *sb, uint64_t pos, void *buf, size_t sz) {
  if (lseek(sb->fd, pos * sb->blksz, SEEK_SET) == -1) 
    return -1;
  
  if (read(sb->fd, buf, sz) == -1) 
    return -1;
  
  return 0;
}

int fs_read_blk(struct superblock *sb, uint64_t pos, void *buf) {
  return fs_read_blk_sz(sb, pos, buf, sb->blksz);
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

int fs_is_invalid_name(const char *name) {
  return strlen(name) == 0 \
    || strncmp(name, ROOT_DIR_NAME, strlen(ROOT_DIR_NAME)) != 0 \
    || strchr(name, ' ') != NULL;
}

uint64_t fs_find_blk(struct superblock *sb, const char *name) {
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

    uint64_t max_links = fs_inode_max_links(sb);

    int i = -1;
    int j = 0;

    while (j < nodeinfo->size && ++i < max_links) {
      if (inode->links[i] == INVALID_BLOCK) {
        continue;
      }

      fs_read_blk(sb, inode->links[i], (void*)child_inode);
      fs_read_blk(sb, child_inode->meta, (void*)child_nodeinfo);

      if (strcmp(child_nodeinfo->name, token) == 0) {
        found = 1;
        blk_pos = inode->links[i];        
        break;
      }

      j++;
    }

    if (found != 1) {
      errno = ENOENT;
      blk_pos = INVALID_BLOCK;
      break;
    }

    token = strtok(NULL, DIR_DELIM_STR);

    if (token == NULL) {
      break;
    }

    if (child_inode->mode != IMDIR) {
      errno = ENOTDIR;
      blk_pos = INVALID_BLOCK;
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
    if (inode->links[i] == INVALID_BLOCK) {
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

  uint64_t max_links = fs_inode_max_links(sb);

  int i = -1;
  int j = 0;

  while (j < nodeinfo->size && ++i < max_links) {
    if (inode->links[i] == INVALID_BLOCK) {
      continue;
    }

    if (inode->links[i] == link_blk) {
      inode->links[i] = INVALID_BLOCK;
      nodeinfo->size--;
      break;
    }

    j++;
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

  if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
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
    root_inode->links[i] = INVALID_BLOCK;
  }

  if (fs_write_blk(sb, ROOT_INODE_BLK, (void*) root_inode) == -1)
    return NULL;

  free(root_inode);

  // ----- Root node info -----

  struct nodeinfo* root_info = (struct nodeinfo*) malloc(blocksize);

  if (root_info == NULL) {
    return NULL;
  }

  strcpy((char*)&root_info->name, ROOT_DIR_NAME);
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

  if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
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
    return INVALID_BLOCK;
  }

  if (sb->freeblks == 0) 
    return 0;

  struct freepage* freepage = (struct freepage*) malloc(sb->blksz);

  if (freepage == NULL) 
    return INVALID_BLOCK;

  if (fs_read_blk(sb, sb->freelist, (void *) freepage) == -1) {
    free(freepage);
    return INVALID_BLOCK;
  }

  uint64_t block = sb->freelist;

  sb->freeblks--;
  sb->freelist = freepage->next;

  if (fs_write_blk(sb, SUPERBLOCK_BLK, (void *) sb) == -1) {
    free(freepage);
    return INVALID_BLOCK;
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
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  if (fs_is_invalid_name(fname)) {
    errno = ENOENT;
    return -1;
  }

  uint64_t max_links = fs_inode_max_links(sb);

  struct inode *inode = (struct inode*) malloc(sb->blksz);
  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  
  uint64_t used_blocks = 0;
  uint64_t used_child_blocks = 0;

  uint64_t needed_blocks = CEIL(cnt, sb->blksz);
  uint64_t needed_child_blocks = MAX(CEIL(needed_blocks, max_links) - 1, 0);

  int new_inode = 0;

  uint64_t block = fs_find_blk(sb, fname);

  // If file already exists
  if (block != INVALID_BLOCK) {
    fs_read_blk(sb, block, (void*) inode);

    if (inode->mode != IMREG) {
      free(inode);
      free(nodeinfo);
      errno = EISDIR;
      return -1;
    }

    fs_read_blk(sb, inode->meta, (void*) nodeinfo);

    used_blocks = CEIL(nodeinfo->size, sb->blksz);
    used_child_blocks = MAX(CEIL(used_blocks, max_links) - 1, 0);
  }

  uint64_t real_needed_blocks = needed_blocks > used_blocks ? needed_blocks - used_blocks : 0;
  uint64_t real_needed_child_blocks = needed_child_blocks > used_child_blocks ? needed_child_blocks - used_child_blocks : 0;

  if (real_needed_blocks + real_needed_child_blocks > sb->freeblks) {
    free(inode);
    free(nodeinfo);
    errno = ENOSPC;
    return -1;
  }

  // If block not exists
  if (block == INVALID_BLOCK) {
    char *basedir = fs_get_basedir(fname);

    uint64_t parent_block = fs_find_blk(sb, basedir);

    free(basedir);

    if (parent_block == INVALID_BLOCK) {
      free(inode);
      free(nodeinfo);
      errno = ENOTDIR;
      return -1;
    }
    
    char *basename = fs_get_basename(fname);

    if (strlen(basename) > fs_nodeinfo_max_name_size(sb)) {
      free(basename);
      free(inode);
      free(nodeinfo);
      errno = ENAMETOOLONG;
      return -1;
    }

    block = fs_get_block(sb);

    if (fs_link_blk(sb, parent_block, block) == -1) {
      free(basename);
      free(inode);
      free(nodeinfo);
      fs_put_block(sb, block);
      return -1;
    }

    inode->mode = IMREG;
    inode->parent = parent_block;
    inode->next = 0;
    inode->meta = fs_get_block(sb);

    strcpy((char*)&nodeinfo->name, basename);

    free(basename);

    new_inode = 1;
  }

  nodeinfo->size = cnt;
  fs_write_blk(sb, inode->meta, (void*) nodeinfo);
  free(nodeinfo);

  uint64_t base_block = block;

  for (int j=0; j<needed_blocks; j++) {
    int i = j % max_links;

    if (i == 0 && j != 0) {
      // If inode already has a child
      if (inode->next != 0) {
        fs_write_blk(sb, block, (void*) inode);
        block = inode->next;
        fs_read_blk(sb, block, (void*) inode);
      } else {
        uint64_t next_block = fs_get_block(sb);

        inode->next = next_block;

        fs_write_blk(sb, block, (void*) inode);

        inode->mode = IMCHILD;
        inode->parent = base_block;
        inode->meta = block;
        inode->next = 0;

        block = next_block;

        new_inode = 1;
      }
    }

    if (inode->links[i] == INVALID_BLOCK || new_inode) {
      inode->links[i] = fs_get_block(sb);
    }

    uint64_t n = (j < needed_blocks - 1) ? sb->blksz : cnt - j * sb->blksz;
    fs_write_blk_sz(sb, inode->links[i], (void*)(buf + j * sb->blksz), n);
  }

  for (int i=(needed_blocks % max_links); i<max_links; i++) {
    // Cleaning remaining allocated link blocks
    if (!new_inode && inode->links[i] != INVALID_BLOCK) {
      fs_put_block(sb, inode->links[i]);
    }

    inode->links[i] = INVALID_BLOCK;
  }

  fs_write_blk(sb, block, (void*)inode);

  // Cleaning remaining allocated child blocks
  while (inode->next != 0) {
    uint64_t next_block = inode->next;

    inode->next = 0;
    fs_write_blk(sb, block, (void*) inode);

    block = next_block;
    fs_read_blk(sb, block, (void*) inode);

    for (int i=0; i<max_links; i++) {
      if (inode->links[i] != INVALID_BLOCK) {
        fs_put_block(sb, inode->links[i]);
      }
    }
    
    fs_put_block(sb, block);
  }

  free(inode);

  return 0;
}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  if (bufsz == 0) {
    return 0;
  }

  if (fs_is_invalid_name(fname)) {
    errno = ENOENT;
    return -1;
  }

  uint64_t block = fs_find_blk(sb, fname);

  if (block == INVALID_BLOCK) {
    errno = ENOENT;
    return -1;
  }

  struct inode *inode = (struct inode*) malloc(sb->blksz);

  fs_read_blk(sb, block, (void*) inode);

  if (inode->mode != IMREG) {
    free(inode);
    errno = EISDIR;
    return -1;
  }

  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  fs_read_blk(sb, inode->meta, (void*) nodeinfo);

  uint64_t nbytes = MIN(nodeinfo->size, bufsz);

  free(nodeinfo);

  uint64_t max_links = fs_inode_max_links(sb);

  uint64_t nlinks = CEIL(nbytes, sb->blksz);

  for (int j=0; j<nlinks; j++) {
    int i = j % max_links;

    if (i == 0 && j != 0) {
      fs_read_blk(sb, inode->next, (void*) inode);
    }

    uint64_t n = (j < nlinks - 1) ? sb->blksz : nbytes - j * sb->blksz;

    fs_read_blk_sz(sb, inode->links[i], buf + j * sb->blksz, n);
  }

  free(inode);
  
  return nbytes;
}

int fs_unlink(struct superblock *sb, const char *fname) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  if (fs_is_invalid_name(fname)) {
    errno = ENOENT;
    return -1;
  }

  uint64_t block = fs_find_blk(sb, fname);

  if (block == INVALID_BLOCK) {
    errno = ENOENT;
    return -1;
  }

  struct inode *inode = (struct inode*) malloc(sb->blksz);

  fs_read_blk(sb, block, (void*) inode);

  if (inode->mode != IMREG) {
    free(inode);
    errno = EISDIR;
    return -1;
  }

  fs_unlink_blk(sb, inode->parent, block);

  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);

  fs_read_blk(sb, inode->meta, (void*) nodeinfo);
  fs_put_block(sb, inode->meta);

  uint64_t max_links = fs_inode_max_links(sb);
  uint64_t nlinks = CEIL(nodeinfo->size, sb->blksz);

  free(nodeinfo);

  for (int j=0; j<nlinks; j++) {
    int i = j % max_links;

    if (i == 0 && j != 0) {
      fs_put_block(sb, block);
      block = inode->next;
      fs_read_blk(sb, inode->next, (void*) inode);
    }

    fs_put_block(sb, inode->links[i]);
  }

  fs_put_block(sb, block);

  free(inode);

  return 0;
}

int fs_mkdir(struct superblock *sb, const char *dname) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  if (fs_is_invalid_name(dname)) {
    errno = ENOTDIR;
    return -1;
  }

  if (sb->freeblks < 2) {
    errno = EBUSY;
    return -1;
  }

  if (fs_find_blk(sb, dname) != INVALID_BLOCK) {
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

  if (parent_blk == INVALID_BLOCK) {
    free(name);
    errno = ENOTDIR;
    return -1;
  }

  uint64_t inode_blk = fs_get_block(sb);

  if (fs_link_blk(sb, parent_blk, inode_blk) == -1) {
    free(name);
    fs_put_block(sb, inode_blk);
    return -1;
  }

  uint64_t nodeinfo_blk = fs_get_block(sb);

  struct nodeinfo *nodeinfo = (struct nodeinfo*) malloc(sb->blksz);
  nodeinfo->size = 0;
  strcpy((char*)&nodeinfo->name, name);

  free(name);

  fs_write_blk(sb, nodeinfo_blk, (void *) nodeinfo);

  free(nodeinfo);

  struct inode *inode = (struct inode*) malloc(sb->blksz);
  inode->mode = IMDIR;
  inode->next = 0;
  inode->parent = parent_blk;
  inode->meta = nodeinfo_blk;

  for (int i=0; i<fs_inode_max_links(sb); i++) {
    inode->links[i] = INVALID_BLOCK;
  }
  
  fs_write_blk(sb, inode_blk, (void *) inode);
  
  free(inode);

  return 0;
}

int fs_rmdir(struct superblock *sb, const char *dname) {
  if (sb->magic != SUPERBLOCK_MAGIC) {
    errno = EBADF;
    return -1;
  }

  if (fs_is_invalid_name(dname)) {
    errno = ENOTDIR;
    return -1;
  }

  uint64_t blk = fs_find_blk(sb, dname);

  if (blk == INVALID_BLOCK) {
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

  if (fs_is_invalid_name(dname)) {
    errno = ENOTDIR;
    return -1;
  }

  uint64_t blk = fs_find_blk(sb, dname);

  if (blk == INVALID_BLOCK) {
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

  uint64_t max_links = fs_inode_max_links(sb);

  int i = -1;
  int j = 0;

  while (j < nodeinfo->size && ++i < max_links) {
    if (inode->links[i] == INVALID_BLOCK) {
      continue;
    }

    fs_read_blk(sb, inode->links[i], (void*) link_inode);
    fs_read_blk(sb, link_inode->meta, (void*) link_nodeinfo);

    strcat(result, link_nodeinfo->name);

    if (link_inode->mode == IMDIR) {
      strcat(result, DIR_DELIM_STR);
    }

    if (i < nodeinfo->size - 1) {
      strcat(result, " ");
    }

    j++;
  }

  free(inode);
  free(nodeinfo);
  free(link_inode);
  free(link_nodeinfo);

  return result;
}