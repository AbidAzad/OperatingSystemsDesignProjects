/*
 *  Copyright (C) 2020 CS416 Rutgers CS
 *	Tiny File System
 *	File:	tfs.c
 *
 */

#define FUSE_USE_VERSION 26
#define S_IFDIR 0x4000
#define S_IFREG 0x8000
#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
struct superblock * superblock ; // superblock
bitmap_t inodeBitmap ; // inode bitmap
bitmap_t blockBitmap ; // data bitmap

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
    bio_read(superblock->i_bitmap_blk, inodeBitmap);

    for (int i = 0; i < MAX_INUM; i++) {
        if (get_bitmap(inodeBitmap, i) == 0) {
            set_bitmap(inodeBitmap, i);
            bio_write(superblock->i_bitmap_blk, inodeBitmap);
            return i;
        }
    }

    return -1;
}


/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    bio_read(superblock->d_bitmap_blk, blockBitmap);

    for (int i = 0; i < MAX_DNUM; i++) {
        if (get_bitmap(blockBitmap, i) == 0) {
            set_bitmap(blockBitmap, i);
            bio_write(superblock->d_bitmap_blk, blockBitmap);
            return superblock->d_start_blk + i;
        }
    }

    return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
    int inodeBlock = superblock->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode)));
    int inodeOffset = ino % (BLOCK_SIZE / sizeof(struct inode));
    struct inode *blockBuffer = (struct inode *)malloc(BLOCK_SIZE);
    bio_read(inodeBlock, (void *)blockBuffer);
    *inode = blockBuffer[inodeOffset];
    free(blockBuffer);

    return 0;
}

int writei(uint16_t ino, const struct inode *inode) {
    int inodeBlock = superblock->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode)));
    int inodeOffset = ino % (BLOCK_SIZE / sizeof(struct inode));

    struct inode *blockBuffer = (struct inode *)malloc(BLOCK_SIZE);
    bio_read(inodeBlock, (void *)blockBuffer);
    blockBuffer[inodeOffset] = *inode;
    bio_write(inodeBlock, (const void *)blockBuffer);
    free(blockBuffer);

    return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
    struct inode *currentInode = (struct inode *)malloc(sizeof(struct inode));
    readi(ino, currentInode);

    struct dirent *dirEntry = (struct dirent *)malloc(BLOCK_SIZE * sizeof(struct dirent));

    for (int i = 0; i < 16; i++) {
        if (currentInode->direct_ptr[i] == 0) {
            return -1; // Not found, return early
        }

        bio_read(currentInode->direct_ptr[i], dirEntry);

        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (dirEntry->valid && strcmp(dirEntry->name, fname) == 0) {
                *dirent = *dirEntry;
                return 0; // Found
            }
            dirEntry++;
        }
    }

    return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname,
            size_t name_len) {
  struct dirent *current_entry = (struct dirent *)malloc(BLOCK_SIZE * sizeof(struct dirent));

  for (int i = 0; i < 16 && dir_inode.direct_ptr[i] != 0; i++) {
    bio_read(dir_inode.direct_ptr[i], current_entry);

    for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
      if (current_entry[j].valid && strcmp(current_entry[j].name, fname) == 0) {
        free(current_entry);
        return -1; // name is already used
      }
    }
  }

  for (int i = 0; i < 16; i++) {
    if (dir_inode.direct_ptr[i] == 0) {
      dir_inode.direct_ptr[i] = get_avail_blkno();
      bio_write(dir_inode.direct_ptr[i], (void *)&dir_inode);
      dir_inode.vstat.st_blocks++;
      if (i < 15) dir_inode.direct_ptr[i + 1] = 0;
    }

    bio_read(dir_inode.direct_ptr[i], current_entry);

    for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
      if (!current_entry[j].valid) {
        current_entry[j].ino = f_ino;
        strncpy(current_entry[j].name, fname, name_len + 1);
        current_entry[j].valid = 1;
        writei(dir_inode.ino, &dir_inode);
        bio_write(dir_inode.direct_ptr[i], current_entry);
        free(current_entry);
        return 0;
      }
    }
  }

  free(current_entry);
  return -1; // directory is full
}


int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
    struct dirent *current_entry = (struct dirent *)malloc(BLOCK_SIZE);

    for (int i = 0; i < 16 && dir_inode.direct_ptr[i] != 0; i++) {
        bio_read(dir_inode.direct_ptr[i], current_entry);

        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (current_entry[j].valid && strcmp(current_entry[j].name, fname) == 0) {
                struct inode *update_inode = (struct inode *)malloc(sizeof(struct inode));
                *update_inode = dir_inode;
                update_inode->size -= sizeof(struct dirent);
                update_inode->vstat.st_size -= sizeof(struct dirent);
                current_entry[j].valid = 0;
                writei(update_inode->ino, update_inode);
                bio_write(dir_inode.direct_ptr[i], (const void *)current_entry);
                free(current_entry);
                free(update_inode);
                return 0;
            }
        }
    }

    free(current_entry);
    return -1; // Entry not found
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t starting_ino, struct inode *result_inode) {
    char *delimiter = "/";
    char *current_token = strtok(path, delimiter);
    struct dirent current_entry = { .ino = 0 };
    if (strcmp(path, delimiter) == 0) { // root
        current_token = NULL;
    }
    while (current_token != NULL) {
        if (dir_find(current_entry.ino, current_token, strlen(current_token), &current_entry) == -1) {
            return -1;
        }
        current_token = strtok(NULL, delimiter);
    }
    readi(current_entry.ino, result_inode);

    return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
    dev_init(diskfile_path);

    // Write superblock information
    superblock = malloc(BLOCK_SIZE);
    *superblock = (struct superblock){.magic_num = MAGIC_NUM, .max_inum = MAX_INUM, .max_dnum = MAX_DNUM,
                                     .i_bitmap_blk = 1, .d_bitmap_blk = 2, .i_start_blk = 3,
                                     .d_start_blk = 3 + (sizeof(struct inode) * MAX_INUM) / BLOCK_SIZE};
    bio_write(0, superblock);

    // Initialize inode and data block bitmaps
    inodeBitmap = malloc(BLOCK_SIZE);
    blockBitmap = malloc(BLOCK_SIZE);
    set_bitmap(inodeBitmap, 0);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    set_bitmap(blockBitmap, 0);
    bio_write(superblock->d_bitmap_blk, blockBitmap);

    // Update inode for root directory
    struct inode *rootNode = malloc(BLOCK_SIZE);
    *rootNode = (struct inode){.ino = 0, .valid = 1, .link = 0, .indirect_ptr = {0},
                               .direct_ptr = {superblock->d_start_blk, 0}, .type = 1,
                               .vstat = {.st_mode = S_IFDIR | 0755, .st_nlink = 2,
                                         .st_blocks = 1, .st_blksize = BLOCK_SIZE}};
    time(&rootNode->vstat.st_mtime);
    bio_write(superblock->i_start_blk, rootNode);
    free(rootNode);

    // Update root directory entries
    struct dirent *rootDir = malloc(BLOCK_SIZE);
    *rootDir = (struct dirent){.ino = 0, .valid = 1};
    strncpy(rootDir->name, ".", 2);
    struct dirent *parent = rootDir + 1;
    *parent = (struct dirent){.ino = 0, .valid = 1};
    strncpy(parent->name, "..", 3);
    bio_write(superblock->d_start_blk, rootDir);
    free(rootDir);

    return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
    if (dev_open(diskfile_path) == -1) {
        rufs_mkfs();
        return NULL;
    }

    superblock = malloc(BLOCK_SIZE);
    inodeBitmap = malloc(BLOCK_SIZE);
    blockBitmap = malloc(BLOCK_SIZE);

    bio_read(0, superblock);
    bio_read(superblock->i_bitmap_blk, inodeBitmap);
    bio_read(superblock->d_bitmap_blk, blockBitmap);

    return NULL;
}

static void rufs_destroy(void *userdata) {
	free(superblock) ;
	free(inodeBitmap) ;
	free(blockBitmap) ;
	dev_close() ;

}

static int rufs_getattr(const char *path, struct stat *attr) {
    struct inode node;

    if (get_node_by_path(path, 0, &node) != 0) {
        return -ENOENT; 
    }

    *attr = node.vstat;

    return 0;
}
static int rufs_opendir(const char *path, struct fuse_file_info *file_info) {
    struct inode *node = (struct inode *)malloc(sizeof(struct inode));
    
    if (get_node_by_path(path, 0, node) == 0 && node->valid) {
        free(node);
        return 0;
    }
    
    free(node);
    return -1;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info) {
    struct inode *node = (struct inode *)malloc(sizeof(struct inode));
    
    if (get_node_by_path(path, 0, node) != 0) {
        free(node);
        return -ENOENT; 
    } 
    
    for (int i = 0; i < 16 && node->direct_ptr[i] != 0; i++) {
        struct dirent *entry = (struct dirent *)malloc(BLOCK_SIZE);
        bio_read(node->direct_ptr[i], entry);
        
        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (entry->valid == 1) {
                struct inode *entry_node = (struct inode *)malloc(sizeof(struct inode));
                readi(entry->ino, entry_node);
                filler(buffer, entry->name, &entry_node->vstat, 0);
                free(entry_node);
            }
            entry++;
        }
        free(entry);
    }

    free(node);
    return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {
    char *parentPath = strdup(path);
    char *dirName = strdup(basename(parentPath));
    dirname(parentPath);

    struct inode parentInode;
    if (get_node_by_path(parentPath, 0, &parentInode) != 0) {
        free(parentPath);
        free(dirName);
        return -ENOENT; // “No such file or directory.”
    }

    int newInodeNumber = get_avail_ino();

    dir_add(parentInode, newInodeNumber, dirName, strlen(dirName));

    struct inode newDirInode = {
        .valid = 1,
        .ino = newInodeNumber,
        .link = 0,
        .direct_ptr = {get_avail_blkno(), 0},
        .indirect_ptr = {0},
        .type = 1,
        .size = sizeof(struct dirent) * 2, // Unix convention
    };

    struct stat newDirStat = {
        .st_mode = S_IFDIR | 0755, // Directory
        .st_nlink = 1,
        .st_ino = newDirInode.ino,
        .st_blocks = 1,
        .st_blksize = BLOCK_SIZE,
        .st_size = newDirInode.size,
    };

    newDirInode.vstat = newDirStat;

    writei(newInodeNumber, &newDirInode);

    struct dirent selfDir = {.ino = newInodeNumber, .valid = 1, .name = "."};
    struct dirent parentDir = {.ino = parentInode.ino, .valid = 1, .name = ".."};

    bio_write(newDirInode.direct_ptr[0], &selfDir);
    bio_write(newDirInode.direct_ptr[0], &parentDir);

    free(parentPath);
    free(dirName);

    return 0;
}

static int rufs_rmdir(const char *path) {
    char *parentPath = strdup(path);
    char *dirName = strdup(basename(parentPath));
    dirname(parentPath);

    struct inode targetInode;
    if (get_node_by_path(path, 0, &targetInode) != 0) {
        free(parentPath);
        free(dirName);
        return -ENOENT; // “No such file or directory.”
    }

    for (int i = 0; i < 16 && targetInode.direct_ptr[i] != 0; i++) {
        unset_bitmap(blockBitmap, targetInode.direct_ptr[i] - superblock->d_start_blk);
        targetInode.direct_ptr[i] = 0;
    }

    bio_write(superblock->d_bitmap_blk, blockBitmap);

    targetInode.valid = 0;
    unset_bitmap(inodeBitmap, targetInode.ino);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    writei(targetInode.ino, &targetInode);

    free(parentPath);
    free(dirName);

    struct inode parentInode;
    if (get_node_by_path(parentPath, 0, &parentInode) != 0) {
        return -ENOENT; // “No such file or directory.”
    }

    dir_remove(parentInode, dirName, strlen(dirName));

    return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char *parentPath = strdup(path);
    char *fileName = strdup(basename(parentPath));
    dirname(parentPath);

    struct inode parentInode;
    if (get_node_by_path(parentPath, 0, &parentInode) != 0) {
        free(parentPath);
        free(fileName);
        return -ENOENT; // “No such file or directory.”
    }

    int newInodeNumber = get_avail_ino();

    dir_add(parentInode, newInodeNumber, fileName, strlen(fileName));

    struct inode newFileInode = {
        .valid = 1,
        .ino = newInodeNumber,
        .link = 0,
        .direct_ptr = {get_avail_blkno(), 0},
        .indirect_ptr = {0},
        .type = 0,
        .size = 0,
    };

    struct stat newFileStat = {
        .st_mode = S_IFREG | 0666, // File
        .st_nlink = 1,
        .st_ino = newFileInode.ino,
        .st_size = newFileInode.size,
        .st_blocks = 1,
    };

    time(&newFileStat.st_mtime);
    newFileInode.vstat = newFileStat;

    writei(newInodeNumber, &newFileInode);

    free(parentPath);
    free(fileName);

    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
    struct inode *node = (struct inode *)malloc(sizeof(struct inode));

    if (get_node_by_path(path, 0, node) == 0 && node->valid) {
        free(node);
        return 0;
    }

    free(node);
    return -1;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct inode *node = (struct inode *)malloc(sizeof(struct inode));

    if (get_node_by_path(path, 0, node) != 0) {
        free(node);
        return -ENOENT; // “No such file or directory.”
    }

    int bytesRead = 0;

    for (int i = 0; i < size && offset < node->size; i++, offset++) {
        int blk = offset / BLOCK_SIZE;
        int off;

        char blockBuffer[BLOCK_SIZE];
        if (blk > 15) { // Large file support
            off = (blk - 16) % (BLOCK_SIZE / sizeof(int));
            blk = (blk - 16) / (BLOCK_SIZE / sizeof(int));
            bio_read(node->indirect_ptr[blk], blockBuffer);
            int *blockAddr = (int *)(blockBuffer + (off * sizeof(int)));
            bio_read(*blockAddr, blockBuffer);
        } else {
            bio_read(node->direct_ptr[blk], blockBuffer);
        }

        buffer[i] = blockBuffer[offset % BLOCK_SIZE];
        bytesRead++;
    }

    free(node);
    return bytesRead;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct inode *node = (struct inode *)malloc(sizeof(struct inode));
    
    if (get_node_by_path(path, 0, node) != 0) {
        free(node);
        return -ENOENT;
    }

    char *blockBuffer = (char *)malloc(BLOCK_SIZE);
    int bytesWritten = 0;

    for (int i = offset; i < (offset + size); i++) {
        int blk = i / BLOCK_SIZE;
        int w = 0;

        if (blk > 15) { 
            int off = (blk - 16) % (BLOCK_SIZE / sizeof(int));
            blk = (blk - 16) / (BLOCK_SIZE / sizeof(int));

            if (node->indirect_ptr[blk] == 0) {
                node->indirect_ptr[blk] = get_avail_blkno();
                if (blk < 7) {
                    node->indirect_ptr[blk + 1] = 0;
                }

                struct inode *newBlock = (struct inode *)malloc(BLOCK_SIZE);
                bio_write(node->indirect_ptr[blk], newBlock);
                free(newBlock);
            }

            bio_read(node->indirect_ptr[blk], blockBuffer);
            blockBuffer = blockBuffer + (off * sizeof(int));
            int temp = *(int *)blockBuffer;

            if (temp == 0) {
                *(int *)blockBuffer = get_avail_blkno();
                temp = *(int *)blockBuffer;
                node->vstat.st_blocks++;
            }

            blockBuffer = blockBuffer - (off * sizeof(int));
            bio_write(node->indirect_ptr[blk], blockBuffer);
            bio_read(temp, blockBuffer);
            w = temp;
        } else {
            if (node->direct_ptr[blk] == 0) {
                node->direct_ptr[blk] = get_avail_blkno();
                if (blk < 15) {
                    node->direct_ptr[blk + 1] = 0;
                }

                struct inode *newBlock = (struct inode *)malloc(BLOCK_SIZE);
                bio_write(node->direct_ptr[blk], newBlock);
                node->vstat.st_blocks++;
                free(newBlock);
            }

            bio_read(node->direct_ptr[blk], blockBuffer);
            w = node->direct_ptr[blk];
        }

        char *a = blockBuffer;
        int j = 0;

        for (; i < (offset + size); i++) {
            *blockBuffer = *buffer;
            node->size++;
            node->vstat.st_size++;
            blockBuffer++;
            j++;
            buffer++;
            bytesWritten++;
            time(&node->vstat.st_mtime);

            if (j >= BLOCK_SIZE) {
                break;
            }
        }

        bio_write(w, a);
        blockBuffer = a;
    }

    writei(node->ino, node);

    free(node);
    free(blockBuffer);
    return bytesWritten;
}

static int rufs_unlink(const char *path) {
    char *parentPath = strdup(path);
    char *fileName = strdup(basename(parentPath));
    dirname(parentPath);

    struct inode targetInode;
    if (get_node_by_path(path, 0, &targetInode) != 0) {
        free(parentPath);
        free(fileName);
        return -ENOENT; // “No such file or directory.”
    }

    for (int i = 0; i < 8; i++) { // Large file support
        if (targetInode.indirect_ptr[i] == 0) {
            break;
        }

        unset_bitmap(blockBitmap, targetInode.indirect_ptr[i] - superblock->d_start_blk);
        targetInode.indirect_ptr[i] = 0;

        int *blockPtr = (int *)malloc(BLOCK_SIZE);
        bio_read(targetInode.indirect_ptr[i], blockPtr);

        for (int j = 0; j < (BLOCK_SIZE / sizeof(int)); j++) {
            if (*blockPtr == 1) {
                unset_bitmap(blockBitmap, *blockPtr - superblock->d_start_blk);
            } else {
                break;
            }
            blockPtr++;
        }

        free(blockPtr);
    }

    for (int i = 0; i < 16; i++) {
        if (targetInode.direct_ptr[i] == 0) {
            break;
        }

        unset_bitmap(blockBitmap, targetInode.direct_ptr[i] - superblock->d_start_blk);
        targetInode.direct_ptr[i] = 0;
    }

    bio_write(superblock->d_bitmap_blk, blockBitmap);

    targetInode.valid = 0;
    unset_bitmap(inodeBitmap, targetInode.ino);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    writei(targetInode.ino, &targetInode);

    free(parentPath);
    free(fileName);

    struct inode parentInode;
    if (get_node_by_path(parentPath, 0, &parentInode) != 0) {
        return -ENOENT; // “No such file or directory.”
    }

    dir_remove(parentInode, fileName, strlen(fileName));

    return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

