#ifndef PROC_DESCRIPTOR_H
#define PROC_DESCRIPTOR_H

#include <limits.h>
#include <types.h>
#include <synch.h>
#include <proc.h>

extern struct lock *proc_lock;
extern struct process_descriptor *process_table[PROC_MAX];
extern int num_processes;
extern int ptable_top;

struct process_descriptor {
	//struct proc *proc;
    bool running;
    pid_t ppid;
    int exit_status;
    struct semaphore *wait_sem;
    //struct lock *wait_lock;
};

void destroy_pdesc(struct process_descriptor *pdesc);

#endif
