#include <stdio.h>
#include "file.h"
#include "inode.h"
#include "diskimg.h"

/**
 * Fetches the specified file block from the specified inode.
 */
int file_getblock(struct unixfilesystem *fs, int inumber, int fileBlockIndex, void *buf) {
    //read the inode
    struct inode inp;
    if (inode_iget(fs, inumber, &inp) < 0) {
        return -1;
    }
    //get the block number
    int blockNum = inode_indexlookup(fs, &inp, fileBlockIndex);
    if (blockNum < 0) {
        return -1;
    }
    //read the block
    if (diskimg_readsector(fs->dfd, blockNum, buf) < 0) {
        return -1;
    }
    //get the number of valid bytes
    int file_size = inode_getsize(&inp);
    if (file_size < 0) {
        return -1;
    }
    int max_block_index = (file_size - 1) / DISKIMG_SECTOR_SIZE;
    if (fileBlockIndex == max_block_index) {
        return (file_size - 1) % DISKIMG_SECTOR_SIZE + 1;
    }
    else {
        return DISKIMG_SECTOR_SIZE;
    }   
}
