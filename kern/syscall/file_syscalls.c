/*
 * AUTHORS: Miraj Kheni. Sam Thomas
 * CREATED ON: 1st March, 2016
 */

#include <kern/errno.h>
#include <types.h>
#include <kern/fcntl.h>
#include <syscall.h>
#include <thread.h>
#include <lib.h>
#include <current.h>
#include <vfs.h>
#include <stat.h>
#include <vnode.h>
#include <synch.h>
#include <uio.h>
#include <copyinout.h>
#include <proc.h>
#include <file_descriptor.h>

int
sys_open(userptr_t filename, int flags, int mode, int *fd)
{
    if(filename == NULL) {
        return EFAULT;
    }

    char buf[NAME_MAX];
    size_t actual_length;
    int err = copyinstr(filename, buf, NAME_MAX, &actual_length);
    if(err != 0) {
        return err;
    }

    int open_flags = flags;

    if (open_flags & O_RDONLY && open_flags & O_WRONLY)
    {
        return EINVAL;
    }
    else if(open_flags & O_RDONLY && open_flags & O_RDWR)
    {
        return EINVAL;
    }
    else if(open_flags & O_WRONLY && open_flags & O_RDWR)
    {
        return EINVAL;
    }
    else
    {
        struct vnode * f_vnode;
        int err = vfs_open(buf, flags, mode, &f_vnode);
        if (err){
            return err;
        }
        else{
            open_flags = flags;
            off_t offset = 0;
            /* If append, set offset at end of file */
            if ((open_flags & O_APPEND) == O_APPEND) {
                struct stat f_stat;
                int err = VOP_STAT(f_vnode, &f_stat);
                if (err) {
                    return err;
                }
                offset = f_stat.st_size;
            }
            int i = 3;
            while(i < OPEN_MAX && curproc->file_table[i] != NULL){
                i++;
            }
            if (i>=OPEN_MAX) {
                return EMFILE;
            }
            else {
                struct file_descriptor *fdesc = kmalloc(sizeof(struct file_descriptor));
                if (fdesc == NULL) {
                    return ENOMEM;
                }
                strcpy(fdesc->filename,(char *)filename);
                fdesc->fdlock = lock_create(fdesc->filename);
                if (fdesc->fdlock == NULL) {
                    kfree(fdesc);
                    return ENOMEM;
                }
                fdesc->offset = offset;
                fdesc->ref_count = 1;
                fdesc->vn = f_vnode;
                fdesc->flags = flags;
                curproc->file_table[i] = fdesc;
                *fd = i;
                return 0;
            }
        }
    }
}

/* @SAM: write this, add the arguments as well */

int
sys_close(int fd)
{
    (void)fd;
    return 0;
}

/* This as well*/
int
sys_read(void)
{
    return 0;
}

int
sys_write(int fd, userptr_t buf, size_t nbytes, size_t *nbytes_written)
{
    struct iovec iov;
    struct uio u_io;

    if (buf == NULL) {
        return EFAULT;
    }

    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    struct file_descriptor *fdesc = curproc->file_table[fd];
    if (fdesc == NULL)
    {
        return EBADF;
    }

    lock_acquire(fdesc->fdlock);
    if (!((fdesc->flags & O_WRONLY) == O_WRONLY || (fdesc->flags & O_RDWR) == O_RDWR))
    {
        lock_release(fdesc->fdlock);
        return EBADF;
    }

    iov.iov_ubase = (userptr_t)buf;
    iov.iov_len = nbytes;
    u_io.uio_iov = &iov;
    u_io.uio_iovcnt = 1;
    u_io.uio_resid = nbytes;
    u_io.uio_offset = fdesc->offset;
    u_io.uio_segflg = UIO_USERSPACE;
    u_io.uio_rw = UIO_WRITE;
    u_io.uio_space = curproc->p_addrspace;

    int result = VOP_WRITE(fdesc->vn, &u_io);
    if (result) {
        lock_release(fdesc->fdlock);
        return result;
    }

    *nbytes_written = nbytes - u_io.uio_resid;
    fdesc->offset += (*nbytes_written);
    lock_release(fdesc->fdlock);
    return 0;
}

/* @SAM */
int
sys_lseek(int fd, off_t pos, int whence, off_t *new_pos)
{
    (void)fd;
    (void)pos;
    (void)whence;
    (void)new_pos;
     return 0;
}

int
sys_dup2(int oldfd, int newfd, int *retfd)
{
    if(oldfd < 0 || oldfd > OPEN_MAX || newfd < 0 || newfd > OPEN_MAX) {
        return EBADF;
    }

    if(curproc->file_table[oldfd] == NULL) {
        return EBADF;
    }

    if(oldfd == newfd) {
        *retfd = newfd;
        return 0;
    }

    if(curproc->file_table[newfd] != NULL) {
        int err = sys_close(newfd);
        if(err != 0) {
            return err;
        }
    }

    curproc->file_table[oldfd]->ref_count++;
    curproc->file_table[newfd] = curproc->file_table[oldfd];
    *retfd = newfd;
    return 0;
}

int
sys_chdir(userptr_t pathname)
{
    if(pathname == NULL) {
        return EFAULT;
    }

    char buf[NAME_MAX];
    size_t actual_length;
    int err = copyinstr(pathname, buf, NAME_MAX, &actual_length);
    if(err != 0) {
        return err;
    }

    err = vfs_chdir(buf);
    if(err != 0) {
        return err;
    }

    return 0;
}

int sys___getcwd(userptr_t buf, size_t buflen, size_t *buflen_written)
{
    struct uio u_io;
    struct iovec iov;
    iov.iov_ubase = buf;
    iov.iov_len = buflen;
    u_io.uio_iov = &iov;
    u_io.uio_iovcnt = 1;
    u_io.uio_resid = buflen;
    u_io.uio_offset = 0;
    u_io.uio_segflg = UIO_USERSPACE;
    u_io.uio_rw = UIO_READ;
    u_io.uio_space = curproc->p_addrspace;
    int err = vfs_getcwd(&u_io);
    if(err != 0) {
        return err;
    }
    *buflen_written = buflen - u_io.uio_resid;
    return 0;
}

// call this function approprietly in sys_close
void
fd_destroy(struct file_descriptor *fdesc)
{
    lock_destroy(fdesc->fdlock);
    kfree(fdesc);
}
