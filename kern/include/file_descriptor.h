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

/* file syscalls definitions */


/* open sustem_call: open a file and assign a file table entry
 *
 * @params:
 * filename: filename to be opened
 * flags: flags to describe the mode the file is to be opened
 * mode: file permissions of the file
 * *fd: file_descriptor(index of the filetable entry) of the file which will be returned to user process
 *
 * return value: 0 on success, error otherwise
 */
int sys_open(userptr_t filename, int flags, int mode, int *fd);

/* @SAM will write this definition */
int sys_read(void);

/* write system call: write the buffer content to file
 *
 * @params:
 * fd: file descriptor of the file
 * buf: userspace buffer whose data is to be written
 * nbytes: number of bytes to be written
 * *nbytes_written: number of bytes actually written, to be returned to the user program
 *
 * return value: 0 on success, error otherwise
 */
int sys_write(int fd, userptr_t buf, size_t nbytes, size_t *nbytes_written);

/* @SAM will write this definition */
int sys_close(int fd);

/* @SAM will write this definition */
int sys_lseek(int fd, off_t pos, int whence, off_t *new_pos);

/* dup2 system call: duplicate the file descriptor to other entry in file table
 *
 * @params:
 * oldfd: file descriptor of the file to be duplicated
 * newfd: file descriptor which will duplicate oldfd
 * *retfd: dile descriptor where the oldfd is duplicated, to be returned to user program
 *
 * return value: 0 on success, error otherwise
 */
int sys_dup2(int oldfd, int newfd, int *retfd);

/* chdir system call: change the current directory to new directory
 *
 * @params:
 * pathname: directory path to be changed to
 *
 * return value: 0 on success, error otherwise
 */
int sys_chdir(userptr_t pathname);

/* __getcwd sistemcall: get current working directory
 * @params:
 * buf: userspace buffer where the current directory path is to be written
 * buflen: size of the buf
 * *buflen_written: bytes written to buf, to be returned to user progam
 *
 * return value: 0 on success, error otherwise
 */
int sys___getcwd(userptr_t buf, size_t buflen, size_t *buflen_written);

#endif
