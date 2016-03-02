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

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <filesys.h>
#include <synch.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
	    return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
	    vfs_close(v);
	    return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
    	/* p_addrspace will go away when curproc is destroyed */
	    vfs_close(v);
	    return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
	    /* p_addrspace will go away when curproc is destroyed */
	    return result;
	}

    /* initilize console */

    char pathname[5];
    strcpy(pathname,"con:");

    /* stdin */
    struct vnode *std_in;
    result = vfs_open(pathname, O_RDONLY, 0664, &std_in);
    if(result) {
         return result;
    }
    struct file_descriptor *fd_stdin = kmalloc(sizeof(struct file_descriptor));
    strcpy(fd_stdin->filename,"con_stdin");
    fd_stdin->flags = O_RDONLY;
    fd_stdin->ref_count = 1;
    fd_stdin->fdlock = lock_create("con_stdin");
    fd_stdin->offset = 0;
    fd_stdin->vn = std_in;

    /* stdout */
    struct vnode *std_out;
    strcpy(pathname,"con:");
    result = vfs_open(pathname, O_WRONLY, 0664, &std_out);
    if(result) {
         return result;
    }
    struct file_descriptor *fd_stdout = kmalloc(sizeof(struct file_descriptor));
    strcpy(fd_stdin->filename,"con_stdout");
    fd_stdout->flags = O_WRONLY;
    fd_stdout->ref_count = 1;
    fd_stdout->fdlock = lock_create("con_stdout");
    fd_stdout->offset = 0;
    fd_stdout->vn = std_out;

    /* stderr */
    struct vnode *std_err;
    strcpy(pathname,"con:");
    result = vfs_open(pathname, O_WRONLY, 0664, &std_err);
    if(result) {
         return result;
    }
    struct file_descriptor *fd_stderr = kmalloc(sizeof(struct file_descriptor));
    strcpy(fd_stdin->filename,"con_stderr");
    fd_stderr->flags = O_RDONLY;
    fd_stderr->ref_count = 1;
    fd_stderr->fdlock = lock_create("con_stderr");
    fd_stderr->offset = 0;
    fd_stderr->vn = std_err;

    curproc->file_table[0] = fd_stdin;
    curproc->file_table[1] = fd_stdout;
    curproc->file_table[2] = fd_stderr;

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
	        NULL /*userspace addr of environment*/,
	        stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

