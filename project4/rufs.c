/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26
#define	S_IFDIR 0x4000
#define	S_IFREG	0x8000
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
struct superblock * superblock ; 
bitmap_t inodeBitmap, blockBitmap ; 

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
    bio_read(superblock->i_bitmap_blk, inodeBitmap);
    
    for (int inodeNumber = 0; inodeNumber < MAX_INUM; inodeNumber++) {
        if (get_bitmap(inodeBitmap, inodeNumber) == 0) {
            set_bitmap(inodeBitmap, inodeNumber);
            bio_write(superblock->i_bitmap_blk, inodeBitmap);
            return inodeNumber;
        }
    }
    
    return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    bio_read(superblock->d_bitmap_blk, blockBitmap);

    for (int blockNumber = 0; blockNumber < MAX_DNUM; blockNumber++) {
        if (get_bitmap(blockBitmap, blockNumber) == 0) {
            set_bitmap(blockBitmap, blockNumber);
            bio_write(superblock->d_bitmap_blk, blockBitmap);
            return superblock->d_start_blk + blockNumber;
        }
    }

    return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

    int block = superblock->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode)));
    int offset = ino % (BLOCK_SIZE / sizeof(struct inode));
    struct inode *tempblock = (struct inode *)malloc(BLOCK_SIZE);
    bio_read(block, (void *)tempblock);
    *inode = tempblock[offset];
    free(tempblock);

    return 0;
}

int writei(uint16_t ino, struct inode *inode) {

    int block = superblock->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode)));
    int offset = ino % (BLOCK_SIZE / sizeof(struct inode));
    struct inode *temp = (struct inode *)malloc(BLOCK_SIZE);
    bio_read(block, (void *)temp);
    temp[offset] = *inode;
    bio_write((const int)block, (const void *)temp);
    free(temp);

    return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
    struct inode currentInode;
    readi(ino, &currentInode);

    struct dirent *currentDataBlock = (struct dirent *)malloc(BLOCK_SIZE);

    for (int i = 0; i < 16 && currentInode.direct_ptr[i] != 0; i++) {
        bio_read(currentInode.direct_ptr[i], currentDataBlock);
        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (currentDataBlock[j].valid && strcmp(currentDataBlock[j].name, fname) == 0) {
                *dirent = currentDataBlock[j];
                free(currentDataBlock);
                return 0;
            }
        }
    }

    free(currentDataBlock);
    return -1;
}

int find_entry(struct inode dir_inode, const char *fname, size_t name_len, struct dirent *found_entry) {
    struct dirent *currentDataBlock = (struct dirent *)malloc(BLOCK_SIZE);

    for (int i = 0; i < 16 && dir_inode.direct_ptr[i] && bio_read(dir_inode.direct_ptr[i], currentDataBlock); i++) {
        struct dirent *entry = currentDataBlock;

        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (entry->valid && strcmp(entry->name, fname) == 0) {
                memcpy(found_entry, entry, sizeof(struct dirent));
                free(currentDataBlock);
                return 0;
            }
            entry++;
        }
    }

    free(currentDataBlock);
    return -1;
}

void update_inode(struct inode *update) {
    update->size += sizeof(struct dirent);
    update->vstat.st_size += sizeof(struct dirent);
    time(&update->vstat.st_mtime);

    writei(update->ino, update);
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
    struct dirent *currentDataBlock = (struct dirent *)malloc(BLOCK_SIZE);

    for (int i = 0; i < 16 && dir_inode.direct_ptr[i] && bio_read(dir_inode.direct_ptr[i], currentDataBlock); i++) {
        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (currentDataBlock[j].valid && strcmp(currentDataBlock[j].name, fname) == 0) {
                free(currentDataBlock);
                return -1;
            }
        }
    }

    struct dirent *directoryEntry;

    for (int i = 0; i < 16; i++) {
        if (!dir_inode.direct_ptr[i]) {
            dir_inode.direct_ptr[i] = get_avail_blkno();
            bio_write(dir_inode.direct_ptr[i], (struct inode *)malloc(BLOCK_SIZE));
            dir_inode.vstat.st_blocks++;
            if (i < 15) dir_inode.direct_ptr[i + 1] = 0;
        }

        bio_read(dir_inode.direct_ptr[i], currentDataBlock);
        directoryEntry = currentDataBlock;

        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (!directoryEntry->valid) {
                directoryEntry->ino = f_ino;
                strncpy(directoryEntry->name, fname, name_len + 1);
                directoryEntry->valid = 1;
                break;
            }
            directoryEntry++;
        }

        if (directoryEntry->valid) break;
    }

    struct inode update = dir_inode;
    update_inode(&update);
    bio_write(dir_inode.direct_ptr[0], currentDataBlock);

    free(currentDataBlock);
    return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
    struct dirent *currentDataBlock = (struct dirent *)malloc(BLOCK_SIZE);

    for (int i = 0; i < 16 && dir_inode.direct_ptr[i] && bio_read(dir_inode.direct_ptr[i], currentDataBlock); i++) {
        struct dirent *directoryEntry = currentDataBlock;

        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
            if (directoryEntry->valid && strcmp(directoryEntry->name, fname) == 0) {
                struct inode update = dir_inode;
                update.size -= sizeof(struct dirent);
                update.vstat.st_size -= sizeof(struct dirent);
                directoryEntry->valid = 0;

                update_inode(&update);
                bio_write(dir_inode.direct_ptr[i], (const void *)currentDataBlock);

                free(currentDataBlock);
                return 0;
            }
            directoryEntry++;
        }
    }

    free(currentDataBlock);
    return -1;
}
/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
    if (strcmp(path, "/") == 0) {
        readi(0, inode);
        return 0;
    }

    char *path_copy = strdup(path);
    char *parsedPath = strtok(path_copy, "/");
    free(path_copy);

    struct dirent directoryEntry;
    directoryEntry.ino = 0;

    while (parsedPath != NULL) {
        if (dir_find(directoryEntry.ino, parsedPath, strlen(parsedPath), &directoryEntry) == -1) {
            return -1;
        }
        parsedPath = strtok(NULL, "/");
    }

    readi(directoryEntry.ino, inode);
    return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
    dev_init(diskfile_path);

    struct superblock *superblock = (struct superblock *)malloc(BLOCK_SIZE);
    *superblock = (struct superblock){
        .magic_num = MAGIC_NUM,
        .max_inum = MAX_INUM,
        .max_dnum = MAX_DNUM,
        .i_bitmap_blk = 1,
        .d_bitmap_blk = 2,
        .i_start_blk = 3,
        .d_start_blk = 3 + ((sizeof(struct inode) * MAX_INUM) / BLOCK_SIZE),
    };
    bio_write(0, superblock);
    free(superblock);

    bitmap_t inodeBitmap = (bitmap_t)malloc(BLOCK_SIZE);
    bitmap_t blockBitmap = (bitmap_t)malloc(BLOCK_SIZE);
    set_bitmap(inodeBitmap, 0);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    set_bitmap(blockBitmap, 0);
    bio_write(superblock->d_bitmap_blk, blockBitmap);

    struct inode root;
    bio_read(superblock->i_start_blk, &root);
    root = (struct inode){
        .ino = 0,
        .valid = 1,
        .link = 0,
        .indirect_ptr = {0},
        .direct_ptr = {superblock->d_start_blk, 0},
        .type = 1,
        .vstat = {
            .st_mode = S_IFDIR | 0755,
            .st_nlink = 2,
            .st_blocks = 1,
            .st_blksize = BLOCK_SIZE,
        },
    };
    time(&root.vstat.st_mtime);
    bio_write(superblock->i_start_blk, &root);

    struct dirent rootDirectory[2];
    rootDirectory[0] = (struct dirent){.ino = 0, .valid = 1, .name = "."};
    rootDirectory[1] = (struct dirent){.ino = 0, .valid = 1, .name = ".."};
    bio_write(superblock->d_start_blk, rootDirectory);

    free(inodeBitmap);
    free(blockBitmap);

    return 0;
}



/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
    if (dev_open(diskfile_path) == -1 ? rufs_mkfs() : 0) return NULL;
    superblock = malloc(BLOCK_SIZE); bio_read(0, superblock);
    inodeBitmap = malloc(BLOCK_SIZE); bio_read(superblock->i_bitmap_blk, inodeBitmap);
    blockBitmap = malloc(BLOCK_SIZE); bio_read(superblock->d_bitmap_blk, blockBitmap);
    return NULL;
}

static void rufs_destroy(void *userdata) {
    free(superblock); free(inodeBitmap); free(blockBitmap);
    dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
    struct inode *foundInode = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, foundInode) != 0) return -ENOENT;
    *stbuf = foundInode->vstat;
    free(foundInode);
    return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
    struct inode *foundInode = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, foundInode) == 0 && foundInode->valid) {
        free(foundInode);
        return 0;
    }
    free(foundInode);
    return -1;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    struct inode *foundInode = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, foundInode) != 0) return -ENOENT;

    for (int i = 0; i < 16 && foundInode->direct_ptr[i] != 0; i++) {
        struct dirent *directoryEntry = malloc(BLOCK_SIZE);
        bio_read(foundInode->direct_ptr[i], directoryEntry);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
            if (directoryEntry->valid == 1) {
                struct inode *k = malloc(sizeof(struct inode));
                readi(directoryEntry->ino, k);
                filler(buffer, directoryEntry->name, &k->vstat, 0);
                free(k);
            }
            directoryEntry++;
        }
        free(directoryEntry);
    }

    free(foundInode);
    return 0;
}

static int create_update_inode(const char *path, mode_t mode, int type) {
    char *directoryPath = strdup(path);
    char *base = strdup(path);
    dirname(directoryPath);
    base = basename(base);

    struct inode *parent = malloc(sizeof(struct inode));
    if (get_node_by_path(directoryPath, 0, parent) != 0) return -ENOENT;

    int avail = get_avail_ino();

    dir_add(*parent, avail, base, strlen(base));

    struct inode *update = malloc(sizeof(struct inode));
    update->valid = 1;
    update->ino = avail;
    update->link = 0;
    update->direct_ptr[0] = get_avail_blkno();
    update->direct_ptr[1] = 0;
    update->indirect_ptr[0] = 0;
    update->type = type;
    update->size = (type == 0) ? 0 : sizeof(struct dirent) * 2;

    struct stat *ustat = malloc(sizeof(struct stat));
    *ustat = (struct stat){
        .st_mode = (type == 0) ? S_IFREG | 0666 : S_IFDIR | 0755,
        .st_nlink = 1,
        .st_ino = update->ino,
        .st_size = update->size,
        .st_blocks = 1
    };
    time(&ustat->st_mtime);
    update->vstat = *ustat;

    writei(avail, update);

    if (type == 1) {
        struct dirent *rootDir = malloc(BLOCK_SIZE);
        *rootDir = (struct dirent){.ino = avail, .valid = 1, .name = "."};
        struct dirent *parent = rootDir + 1;
        *parent = (struct dirent){.ino = parent->ino, .valid = 1, .name = ".."};
        bio_write(update->direct_ptr[0], rootDir);
        free(rootDir);
    }

    free(ustat);
    free(update);

    return 0;
}
static int rufs_mkdir(const char *path, mode_t mode) {
    return create_update_inode(path, mode, 1);
}

static int rufs_rmdir(const char *path) {
    char *directoryPath = strdup(path);
    char *base = strdup(path);
    dirname(directoryPath);
    base = basename(base);

    struct inode *target = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, target) != 0) return -ENOENT;

    for (int i = 0; i < 16 && target->direct_ptr[i] != 0; i++) {
        unset_bitmap(blockBitmap, target->direct_ptr[i] - superblock->d_start_blk);
        target->direct_ptr[i] = 0;
    }
    bio_write(superblock->d_bitmap_blk, blockBitmap);

    target->valid = 0;
    unset_bitmap(inodeBitmap, target->ino);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    writei(target->ino, target);
    free(target);

    struct inode *parent = malloc(sizeof(struct inode));
    if (get_node_by_path(directoryPath, 0, parent) != 0) return -ENOENT;

    dir_remove(*parent, base, strlen(base));

    free(parent);
    return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    return create_update_inode(path, mode, 0);
}
static int rufs_open(const char *path, struct fuse_file_info *fi) {

	struct inode *foundInode = malloc(sizeof(struct inode));
	if (get_node_by_path(path, 0, foundInode) == 0 && foundInode->valid) {
		free(foundInode);
		return 0;
	}
	free(foundInode);
	return -1;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct inode *foundInode = (struct inode *)malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, foundInode) != 0)
        return -ENOENT;  

    int bytesRead = 0;
    int fileSize = size + offset;

    while (offset < fileSize && bytesRead < (int)size) {
        int blk = offset / BLOCK_SIZE;
        int off = (blk > 15) ? ((blk - 16) % (BLOCK_SIZE / sizeof(int))) : 0;
        blk = (blk > 15) ? ((blk - 16) / (BLOCK_SIZE / sizeof(int))) : blk;

        char *b = (char *)malloc(BLOCK_SIZE);
        bio_read((blk > 15) ? *((int *)(b + off)) : foundInode->direct_ptr[blk], b);

        int j = 0;
        while (offset < fileSize && j < BLOCK_SIZE && bytesRead < (int)size) {
            *buffer++ = b[j++];
            offset++;
            bytesRead++;
        }

        free(b);
    }

    return bytesRead;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct inode *foundInode = (struct inode *)malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, foundInode) != 0)
        return -ENOENT;

    char *blockBuffer = (char *)malloc(BLOCK_SIZE);
    int bytesWritten = 0;

    for (int i = offset; i < offset + size; i++) {
        int blk = i / BLOCK_SIZE;
        int *indirectPtr = (blk > 15) ? &foundInode->indirect_ptr[blk - 16] : &foundInode->direct_ptr[blk];
        int w = 0;

        if (*indirectPtr == 0) {
            *indirectPtr = get_avail_blkno();
            if (blk < (blk > 15 ? 7 : 15))
                indirectPtr[1] = 0;

            struct inode *newBlock = (struct inode *)malloc(BLOCK_SIZE);
            bio_write(*indirectPtr, newBlock);
            free(newBlock);
            foundInode->vstat.st_blocks++;
        }

        bio_read(*indirectPtr, blockBuffer);
        w = *((int *)(blockBuffer + ((blk > 15) ? (blk - 16) % (BLOCK_SIZE / sizeof(int)) : 0)));

        if (w == 0) {
            *((int *)(blockBuffer + ((blk > 15) ? (blk - 16) % (BLOCK_SIZE / sizeof(int)) : 0))) = get_avail_blkno();
            w = *((int *)(blockBuffer + ((blk > 15) ? (blk - 16) % (BLOCK_SIZE / sizeof(int)) : 0)));
            foundInode->vstat.st_blocks++;
        }

        for (int j = 0; i < offset + size && j < BLOCK_SIZE; i++, j++) {
            blockBuffer[j] = *buffer;
            foundInode->size++;
            foundInode->vstat.st_size++;
            buffer++;
            bytesWritten++;
            time(&foundInode->vstat.st_mtime);
        }

        bio_write(w, blockBuffer);
    }

    writei(foundInode->ino, foundInode);
    free(blockBuffer);
    free(foundInode);
    return bytesWritten;
}

static int rufs_unlink(const char *path) {
    char *directoryPath = strdup(path);
    char *base = basename(strdup(path));

    struct inode *target = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, target) != 0) return -ENOENT;

    for (int i = 0; i < 8 && target->indirect_ptr[i]; i++) {
        unset_bitmap(blockBitmap, target->indirect_ptr[i] - superblock->d_start_blk);
        target->indirect_ptr[i] = 0;

        int *a = malloc(BLOCK_SIZE);
        bio_read(target->indirect_ptr[i], a);

        for (int j = 0; j < BLOCK_SIZE / sizeof(int) && *a == 1; j++) {
            unset_bitmap(blockBitmap, *a - superblock->d_start_blk);
            a++;
        }
    }

    for (int i = 0; i < 16 && target->direct_ptr[i]; i++) {
        unset_bitmap(blockBitmap, target->direct_ptr[i] - superblock->d_start_blk);
        target->direct_ptr[i] = 0;
    }

    bio_write(superblock->d_bitmap_blk, blockBitmap);

    target->valid = 0;
    unset_bitmap(inodeBitmap, target->ino);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    writei(target->ino, target);
    free(target);

    struct inode *parent = malloc(sizeof(struct inode));
    if (get_node_by_path(directoryPath, 0, parent) != 0) return -ENOENT;

    dir_remove(*parent, base, strlen(base));

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

