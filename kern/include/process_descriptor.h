#ifndef PROCSYS_H
#define PROCSYS_H

#include <limits.h>
#include <types.h>
#include <synch.h>

struct process_descriptor {
    bool running;
    pid_t ppid;
    int exit_status;
    struct cv *wait_cv;
    struct lock *wait_lock;
    struct proc *process;
};

void destroy_pdesc(struct process_descriptor);
