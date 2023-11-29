#include <sys/types.h>
#include "fs.h"

struct superblock * fs_format(const char *fname, uint64_t blocksize) {

}

struct superblock * fs_open(const char *fname) {

}

int fs_close(struct superblock *sb) {

}

uint64_t fs_get_block(struct superblock *sb) {

}

int fs_put_block(struct superblock *sb, uint64_t block) {

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