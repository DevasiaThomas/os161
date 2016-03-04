#ifndef PROC_DESCRIPTOR_H
#define PROC_DESCRIPTOR_H

#include <limits.h>
#include <types.h>
#include <synch.h>

extern struct lock *proc_lock;
extern struct process_descriptor *process_table[PID_MAX];

struct process_descriptor {
    bool running;
    pid_t ppid;
    int exit_status;
    struct cv *wait_cv;
    struct lock *wait_lock;
    struct proc *process;
};

void destroy_pdesc(struct process_descriptor);
#endif
