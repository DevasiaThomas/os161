#ifndef FILE_DESCRIPTOR_H
#define FILE_DESCRIPTOR_H

#include <limits.h>
#include <types.h>

struct file_descriptor {
    char filename[NAME_MAX];
    int flags;
    off_t offset;
    int ref_count;
    struct vnode *vn;
    struct lock *fdlock;
};

void fd_destroy(struct file_descriptor *);

#endif
