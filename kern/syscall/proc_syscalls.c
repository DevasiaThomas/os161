/*
 * AUTHORS: Miraj Kheni. Sam Thomas
 * CREATED ON: 2nd March, 2016
 */

#include <kern/errno.h>
#include <types.h>
#include <kern/fcntl.h>
#include <kern/wait.h>
#include <machine/vm.h>
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
        kfree(child_tf);
        return err;
    }
    err = thread_fork("child", child_proc, childproc_init, child_tf, 0);
    if(err) {
        kfree(child_tf);
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

int
sys_execv(userptr_t u_progname, userptr_t u_args)
{
    char *progname = kmalloc(NAME_MAX * sizeof(char));
    size_t actual;
    int err;
    err = copyinstr(u_progname, progname, NAME_MAX, &actual);
    if(err) {
        kfree(progname);
        return err;
    }
    if(strcmp(progname,"") == 0) {
        kfree(progname);
        return EISDIR;
    }

    int i = 0;
    int copy_len = 0;
    int arg_len_left = ARG_MAX;
    int arg_len = 0;
    int padding = 0;
    char **p_arg = kmalloc(sizeof(char *));
    char *args = kmalloc(65520);
    char *temp = args;
    while(1) {

        err = copyin(u_args, p_arg, sizeof(char *));
        if(err) {
            kfree(progname);
            kfree(args);
            kfree(p_arg);
            return err;
        }
        if(*p_arg == NULL) {
            kfree(p_arg);
            kfree(args);
            break;
        }
        if(err) {
            kfree(progname);
            kfree(p_arg);
            kfree(args);
            return err;
        }
        err =  copyinstr((const_userptr_t)*p_arg,temp,arg_len_left,&actual);
        if(err) {
            kfree(progname);
            kfree(p_arg);
            kfree(args);
            return err;
        }
        arg_len = actual + 1;
        padding = (4 - ((arg_len + 1) % 4)) % 4; //padding needed to align by 4
        arg_len_left -= (arg_len + 1);
        if(arg_len_left <= 0) {
            kfree(progname);
            kfree(p_arg);
            kfree(args);
            return E2BIG;
        }
        copy_len += arg_len + padding + 1;
        u_args += sizeof(char *);
        temp += actual;
        i++;
    }

    int argc = i;
    /* Run program */

    struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	/* Open the file. */
	err = vfs_open(progname, O_RDONLY, 0, &v);
	if (err) {
        kfree(progname);
        kfree(args);
	    return err;
	}

    kfree(progname);
    struct addrspace *cur_as = proc_getas();

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
        vfs_close(v);
        kfree(args);
	    return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
    err = load_elf(v, &entrypoint);
	if (err) {
        if(cur_as) {
            as_destroy(curproc->p_addrspace);
            proc_setas(cur_as);
        }
    	/* p_addrspace will go away when curproc is destroyed */
	    vfs_close(v);
        kfree(args);
	    return err;
	}


	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
    err = as_define_stack(as, &stackptr);
	if (err) {
        if(cur_as) {
           as_destroy(curproc->p_addrspace);
            proc_setas(cur_as);
        }
	    /* p_addrspace will go away when curproc is destroyed */
        kfree(args);
	    return err;
	}

    copy_len += 4*(argc + 1);

    /* set the stackptr */
    stackptr -= copy_len;
    int prev_offset = 0;
    char **ret_buf = kmalloc((argc + 1)*sizeof(char *));
    char *dest = NULL;
    /* moving contents from kernel buffer to user stack */

    temp = args;
    for (i = 0; i < argc; i++)
    {
        arg_len = strlen(temp);
        padding = (4 - ((arg_len + 1) % 4)) % 4;
        dest = (char *)stackptr + (argc + 1) * 4 + prev_offset;
        err = copyout(temp, (userptr_t)dest, (size_t)arg_len + 1);
        if (err) {
            if(cur_as) {
                as_destroy(curproc->p_addrspace);
                proc_setas(cur_as);
                kfree(args);
            }
            kfree(ret_buf);
            return err;
        }

        for (int k = arg_len; k < arg_len + padding; k++)
        {
            dest[k] = '\0';
        }
        ret_buf[i] = (char *)dest;
        prev_offset += (arg_len + padding + 1);
        temp += arg_len + 1;
    }

    ret_buf[argc] = NULL;
    err = copyout(ret_buf,(userptr_t)stackptr, (argc+1)*sizeof(char *));
    if (err) {
        if(cur_as) {
            as_destroy(curproc->p_addrspace);
            proc_setas(cur_as);
            kfree(args);
        }
        return err;
    }

    kfree(ret_buf);
    kfree(args);
    as_destroy(cur_as);
    enter_new_process(argc, (userptr_t)stackptr,
            NULL /*userspace addr of environment*/,
            stackptr, entrypoint);

    /* enter_new_process does not return. */
    	panic("enter_new_process returned\n");
    return EINVAL;
}

pid_t
sys_getpid()
{
return curproc->pid;
}

void sys_exit(int exitcode){//sam 03/05
	int i = curproc->pid;
    struct proc *cur = curproc;
	struct process_descriptor *pdesc = process_table[i];
	if((pdesc->ppid == -1)||( pdesc->ppid != 0 && ((process_table[pdesc->ppid] == NULL)||(WIFEXITED(process_table[pdesc->ppid]->exit_status))))){//orphan process
		proc_destroy(cur);
		destroy_pdesc(pdesc);
		pdesc = process_table[i] = NULL;
	}
	else{
		proc_destroy(cur);
		pdesc->running = false;
		pdesc->exit_status = _MKWAIT_EXIT(exitcode);
		V(pdesc->wait_sem);
	}
    thread_exit();
}

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retpid){//sam 03/06
    if(pid < PID_MIN || pid > PID_MAX) {
         return ESRCH;
    }
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
		if(!(((unsigned long)status & (sizeof(int)-1)) == 0)){// to check if status pointer is alligned
            if(curproc->pid == 0) { //menuthread
                 destroy_pdesc(pdesc);
                 pdesc = process_table[pid] = NULL;
            }
			return EFAULT;
		}
		int err=copyout(&pdesc->exit_status,status,sizeof(int)); // I am not sure how to put a value into a userptr directly
		if(err){
            if(curproc->pid == 0) { //menuthread
                 destroy_pdesc(pdesc);
                 pdesc = process_table[pid] = NULL;
            }
			return err;
		}
	}

	destroy_pdesc(pdesc);
	pdesc = process_table[pid] = NULL;
	return 0;
}

int
sys_sbrk(intptr_t amount, int *retval)
{
    struct addrspace *as = proc_getas();
    if(amount >= 0) {
        if((vaddr_t)amount <= USERSTACKTOP - as->heap_end) {
            *retval = as->heap_end;
            as->heap_end += amount;
            return 0;
        }
        else {
            *retval = -1;
            return ENOMEM;
        }
    }
    else if(as->heap_end + (vaddr_t)amount >= as->heap_start) {
        *retval = as->heap_end;
        free_pages(as, as->heap_end+amount, as->heap_end);
        as->heap_end += amount;
        return 0;
    }
    else {
        *retval = -1;
        return EINVAL;
    }
}
