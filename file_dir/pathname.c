#include "pathname.h"
#include "directory.h"
#include "inode.h"
#include "diskimg.h"
#include <stdio.h>
#include <string.h>
#define DIRNAME_MAX_LEN 14

/**
 * Returns the inode number associated with the specified pathname recursively.
 * Helper function for pathname_lookup.
 */
int recursive_pathname_lookup(struct unixfilesystem *fs, int inumber, const char* pathname) {
    //check for slashes
    char* first_slash = strchr(pathname, '/');
    if(first_slash == NULL) {
        struct direntv6 direntry;
        if (directory_findname(fs, pathname, inumber, &direntry) < 0) {
            return -1;
        }
        return direntry.d_inumber;
    }
    //derive the new pathname
    char* new_pathname = first_slash + strlen("/");
    int direntry_len = strlen(pathname) - strlen(new_pathname) - 1;
    char direntry_name[DIRNAME_MAX_LEN];
    strncpy(direntry_name, pathname, direntry_len);
    //null-termination for some directory names
    if (direntry_len < DIRNAME_MAX_LEN) {
        direntry_name[direntry_len] = '\0';
    }    
    //find the directory entry
    struct direntv6 direntry;
    if (directory_findname(fs, direntry_name, inumber, &direntry) < 0) {
        return -1;
    }
    return recursive_pathname_lookup(fs, direntry.d_inumber, new_pathname);
}    

/**
 * Returns the inode number associated with the specified pathname.
 */
int pathname_lookup(struct unixfilesystem *fs, const char *pathname) {
    if (strcmp(pathname, "/") == 0) {
        return ROOT_INUMBER;
    }
    //delete the first slash
    const char* new_pathname = pathname + strlen("/");
    return recursive_pathname_lookup(fs, ROOT_INUMBER, new_pathname);
}

