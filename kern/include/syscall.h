/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYSCALL_H_
#define _SYSCALL_H_


#include <cdefs.h> /* for __DEAD */
struct trapframe; /* from <machine/trapframe.h> */

/*
 * The system call dispatcher.
 */

void syscall(struct trapframe *tf);

/*
 * Support functions.
 */

/* Helper for fork(). You write this. */
void enter_forked_process(struct trapframe *tf);

/* Enter user mode. Does not return. */
__DEAD void enter_new_process(int argc, userptr_t argv, userptr_t env,
		       vaddr_t stackptr, vaddr_t entrypoint);


/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);
int sys___time(userptr_t user_seconds, userptr_t user_nanoseconds);

/* File System calls */

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
int sys_read(int fd, userptr_t buf, size_t nbytes, size_t *nbytes_read);

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


/* Process System calls */
int sys_fork(struct trapframe *tf, pid_t *ret_pid);
int sys_getpid(void);

#endif /* _SYSCALL_H_ */
