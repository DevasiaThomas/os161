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
    struct trapframae *child_tf = kmalloc(sizeof(struct trapframe));
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
