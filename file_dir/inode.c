#include <stdio.h>
#include <stdlib.h>
#include "inode.h"
#include "diskimg.h"
#define INO_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(struct inode))
#define BNUMS_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(uint16_t))
#define INDIR_ADDR 7

/**
 * Fetches the specified inode from the filesystem. 
 */
int inode_iget(struct unixfilesystem *fs, int inumber, struct inode *inp) {
    inumber -= 1;
    int sector_index = inumber / INO_PER_BLOCK;
    int inode_index = inumber % INO_PER_BLOCK;    
    if (inumber < 0 || sector_index >= fs->superblock.s_isize) {
        return -1;
    }
    //read a sector
    struct inode inodes[INO_PER_BLOCK];
    if (diskimg_readsector(fs->dfd, INODE_START_SECTOR + sector_index, inodes) < 0) {
        return -1;
    }
    //identify the right inode
    *inp = inodes[inode_index];
    return 0;
}

/**
 * Given an index of a file block, retrieves the file's actual block number
 * from the given inode. 
 */
int inode_indexlookup(struct unixfilesystem *fs, struct inode *inp, int fileBlockIndex){
    int fd = fs->dfd;
    int inode_size = inode_getsize(inp);
    if (fileBlockIndex < 0 || fileBlockIndex > (inode_size - 1) / DISKIMG_SECTOR_SIZE) {
        return -1;
    }
    //check file size    
    if ((inp->i_mode & ILARG) == 0) {
        return inp->i_addr[fileBlockIndex];
    }
    int max_sindir_blocks_size = INDIR_ADDR * BNUMS_PER_BLOCK;
    if (fileBlockIndex < max_sindir_blocks_size) {
        int sindir_block_index = fileBlockIndex / BNUMS_PER_BLOCK;
        int dir_block_index = fileBlockIndex % BNUMS_PER_BLOCK;                
        //read an indirect block
        uint16_t dir_bnums[BNUMS_PER_BLOCK];
        if (diskimg_readsector(fd, inp->i_addr[sindir_block_index], dir_bnums) < 0) {
            return -1;
        }
        //identify the right direct block
        return dir_bnums[dir_block_index];
    }
    else {
        int total_dir_bnums = fileBlockIndex - max_sindir_blocks_size;
        int sindir_block_index = total_dir_bnums / BNUMS_PER_BLOCK;                
        //read a doubly indirect block
        uint16_t sindir_bnums[BNUMS_PER_BLOCK];
        if (diskimg_readsector(fd, inp->i_addr[INDIR_ADDR], sindir_bnums) < 0) {
            return -1;
        }        
        int dir_block_index = total_dir_bnums % BNUMS_PER_BLOCK;
        //read a singly indirect block
        uint16_t dir_bnums[BNUMS_PER_BLOCK];
        if (diskimg_readsector(fd, sindir_bnums[sindir_block_index], dir_bnums) < 0) {
            return -1;
        }
        return dir_bnums[dir_block_index];
    }
    return -1;
}

int inode_getsize(struct inode *inp) {
    return ((inp->i_size0 << 16) | inp->i_size1); 
}
