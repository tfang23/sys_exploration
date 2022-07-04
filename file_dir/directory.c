#include "directory.h"
#include "inode.h"
#include "diskimg.h"
#include "file.h"
#include <stdio.h>
#include <string.h>
#define DIR_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(struct direntv6))

/**
 * Looks up the specified name (name) in the specified directory (dirinumber).  
 */
int directory_findname(struct unixfilesystem *fs, const char *name,
		       int dirinumber, struct direntv6 *dirEnt) {
    //read the inode
    struct inode inp;
    if (inode_iget(fs, dirinumber, &inp) < 0) {
        return -1;
    }
    //check if it's a directory
    if ((inp.i_mode & IFMT) != IFDIR) {
        return -1;
    }
    //get directory size
    int dir_size = inode_getsize(&inp);
    if (dir_size <= 0) {
        return -1;
    }
    //check all blocks
    int total_direct_blocks = (dir_size - 1) / DISKIMG_SECTOR_SIZE + 1;
    for (int b = 0; b < total_direct_blocks; b++) {
        struct direntv6 direntries[DIR_PER_BLOCK];
        int block_size = file_getblock(fs, dirinumber, b, direntries);
        if (block_size < 0) {
            return -1;
        }
        //check all directory entries in the block
        int total_direntries = block_size / sizeof(struct direntv6);
        for (int i = 0; i < total_direntries; i++) {
            if (strncmp(name, direntries[i].d_name, strlen(name)) == 0) {
                *dirEnt = direntries[i];
                return 0;
            }
        }
    }
    return -1;
}
