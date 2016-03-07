/*
 * AUTHORS: Miraj Kheni. Sam Thomas
 * CREATED ON: 2nd March, 2016
 */

#include <kern/errno.h>
#include <types.h>
#include <kern/fcntl.h>
#include <kern/wait.h>
#include <syscall.h>
#include <thread.h>
#include <lib.h>
#include <current.h>
#include <process_descriptor.h>
#include <vfs.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <copyinout.h>
#include <addrspace.h>
#include <proc.h>
#include <file_descriptor.h>

void childproc_init(void *tf, unsigned long junk);

int
sys_fork(struct trapframe *tf, pid_t *ret_pid)
{
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if(child_tf == NULL) {
        return ENOMEM;
    }
    memmove(child_tf, tf, sizeof(struct trapframe));

    int err = 0;
    struct proc *child_proc = proc_fork("child",&err);
    if(err) {
        return err;
    }
    err = thread_fork("child", child_proc, childproc_init, child_tf, 0);
    if(err) {
         return err;
    }

    *ret_pid = child_proc->pid;
    return 0;
}

void
childproc_init(void *tf, unsigned long junk)
{
    (void)junk;
    struct trapframe child_tf;
    memmove(&child_tf,tf,sizeof(struct trapframe));
    kfree(tf);
    child_tf.tf_v0 = 0;
    child_tf.tf_a3 = 0;
    child_tf.tf_epc += 4;
    mips_usermode(&child_tf);
}

pid_t
sys_getpid()
{
return curproc->pid;
}

void sys_exit(int exitcode){//sam 03/05
	int i = curproc->pid;
	struct process_descriptor *pdesc = process_table[i];
	if((pdesc->ppid == -1)||(process_table[pdesc->ppid] == NULL)||(WIFEXITED(process_table[pdesc->ppid]->exit_status))){//orphan process
		proc_destroy(pdesc->proc);
		destroy_pdesc(pdesc);
		pdesc = process_table[i] = NULL;
	}
	else{
		proc_destroy(pdesc->proc);
		pdesc->running = false;
		pdesc->exit_status = _MKWAIT_EXIT(exitcode);	
		V(pdesc->wait_sem);
	}
    thread_exit();
}

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retpid){//sam 03/06
	struct process_descriptor *pdesc = process_table[pid];
	*retpid = pid;

	if(pdesc == NULL){// checking for invalid pdesc
		return ESRCH;
	}
	if(curproc->pid != pdesc->ppid){
		return ECHILD;
	}
	if(!(options == 0 || options == WNOHANG)){
		return EINVAL;
	}
	while(pdesc->running){	//wait for child to exit
		if(options == WNOHANG){
			*retpid = 0;
			return 0;
		}
		P(pdesc->wait_sem);
	}
	if(status!=NULL){
		if(!(((unsigned long)status & (sizeof(int)-1)) == 0)){ // to check if status pointer is alligned
			return EFAULT;
		}
		int err=copyout((const void *)&pdesc->exit_status,status,sizeof(int)); // I am not sure how to put a value into a userptr directly
		if(err){
			return err;
		}
	}

	destroy_pdesc(pdesc);
	pdesc = process_table[pid] = NULL;
	return 0;
}

