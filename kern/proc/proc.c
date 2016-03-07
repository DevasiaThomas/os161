/*
 * Copyright (c) 2013
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
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */
#include <kern/errno.h>
#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <synch.h>
#include <vnode.h>
#include <vfs.h>
#include <limits.h>
#include <kern/wait.h>
#include <file_descriptor.h>
#include <process_descriptor.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;
struct lock *proc_lock;
struct process_descriptor *process_table[PID_MAX];
int num_processes;
int ptable_top;

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

    /* initialize file table */
    unsigned i;
    for(i = 0;i < OPEN_MAX; i++ ) {
         proc->file_table[i] = NULL;
    }

    proc->pid = -1; //initialize to -1
    proc->p_numthreads = 0;
    spinlock_init(&proc->p_lock);

    /* VM fields */
    proc->p_addrspace = NULL;

    /* VFS fields */
    proc->p_cwd = NULL;

    if(strcmp(name,"[kernel]") == 0) {
         return proc;
    }

    for(i = ptable_top; i <= PID_MAX; i++ ) {
        lock_acquire(proc_lock);
        if(process_table[i] == NULL) {
            struct process_descriptor *pdesc = kmalloc(sizeof(struct process_descriptor));
            if(pdesc == NULL) {
                spinlock_cleanup(&proc->p_lock);
                kfree(proc->p_name);
                kfree(proc);
                lock_release(proc_lock);
                return NULL;
            }

            pdesc->wait_sem = sem_create(proc->p_name,0);
            if(pdesc->wait_sem == NULL) {
                spinlock_cleanup(&proc->p_lock);
                kfree(proc->p_name);
                kfree(pdesc);
                kfree(proc);
                lock_release(proc_lock);
                return NULL;
            }

            pdesc->running = true;
            pdesc->exit_status = -1;
            if(curproc == NULL) {
                pdesc->ppid = -1;
            }
            else {
                pdesc->ppid = curproc->pid;
            }
            proc->pid = i;
			pdesc->proc = proc;
            process_table[i] = pdesc;
            num_processes++;
            ptable_top = i;
            lock_release(proc_lock);
            break;
        }
        lock_release(proc_lock);
        if(i == PID_MAX) {
            i = PID_MIN;
        }
    }

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);
	
	
	proc_remthread(curthread);// to let the proc be destroyed while exiting before thread_exit().

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	int i;
    for(i = 0;i < OPEN_MAX; i++ ) {
		if(proc->file_table[i] != NULL){
			if(lock_do_i_hold(proc->file_table[i]->fdlock)){
				lock_release(proc->file_table[i]->fdlock);
			}
			if(proc->file_table[i]->ref_count <= 1){ //if exit was called when reference count was just 1 implies this process held last refernce
				vfs_close(proc->file_table[i]->vn);
				fd_destroy(proc->file_table[i]);
			}
			else{
				proc->file_table[i]->ref_count -=1;
			}
			proc->file_table[i] = NULL;
		}
    }
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}

//destroy the pdesc created in proc_fork-> Sam03/05
void destroy_pdesc(struct process_descriptor *pdesc){
	sem_destroy(pdesc->wait_sem);
	pdesc->proc = NULL;
	kfree(pdesc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}

    if(proc_lock == NULL) {
         proc_lock = lock_create("proc_lock");
         if(proc_lock == NULL) {
            panic("proc_lock failed\n");
         }
    }


    num_processes = 0;
    ptable_top = PID_MIN;
    int i;
    for(i = 0; i < PID_MAX; i++) {
        process_table[i] = NULL;
    }
}

struct proc *
proc_fork(const char *name, int *err)
{
    struct proc *child_proc = proc_create(name);
    if(child_proc == NULL) {
         *err = ENOMEM;
         return NULL;
    }
    if(num_processes > PROC_MAX) {
        kfree(child_proc->p_name);
        kfree(child_proc);
        *err = ENPROC;
        return NULL;
    }

    /* copy file table */
    int i;
    for(i = 0; i < OPEN_MAX; i++) {
        if(curproc->file_table[i] != NULL) {
            curproc->file_table[i]->ref_count++;
        }
            child_proc->file_table[i] = curproc->file_table[i];
    }

    int errnum;
    /* set address_space */
    if(curproc->p_addrspace != NULL) {
         struct addrspace *child_as;
         errnum = as_copy(curproc->p_addrspace, &child_as);
         if(errnum) {
            kfree(child_proc->p_name);
            kfree(child_proc);
            *err = errnum;
            return NULL;
         }
         child_proc->p_addrspace = child_as;
    }

    return child_proc;
}


/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
