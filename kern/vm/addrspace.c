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

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <lib.h>
#include <spl.h>
#include <synch.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <machine/vm.h>
#include <vm.h>
#include <proc.h>
#include <vnode.h>
#include <uio.h>
#include <vfs.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/*
static
void
free_page_table(struct page_table_entry *pte) {
    if(pte == NULL) {
        return;
    }
    struct page_table_entry *t_pte = pte;
    while(t_pte != NULL) {
        struct page_table_entry *temp = t_pte->next;
        kfree(t_pte);
        t_pte = temp;
    }
    return;

}
*/

char kbuf[PAGE_SIZE];

int copy_page_table(struct addrspace *, struct addrspace *newas);
void free_list(struct region_entry**);
void free_page_table(struct page_table_entry ******);
int copy_regions(struct addrspace *, struct region_entry **);
void free_pte(struct addrspace *,vaddr_t vaddr);

int
copy_page_table(struct addrspace *old_as, struct addrspace *new_as)
{
/*  multi-level page table */
    struct page_table_entry *****old_page_table = old_as->page_table;
    struct page_table_entry *****new_page_table = new_as->page_table;

    for(int i=0; i < 256; i++) {
        if(old_page_table[i] != NULL) {
            new_page_table[i] = kmalloc(256*sizeof(struct page_table_entry ***));
            if(new_page_table[i] == NULL) {
                return ENOMEM;
            }
            for(int j = 0; j < 256; j++) {
                if(old_page_table[i][j] != NULL) {
                    new_page_table[i][j] = kmalloc(256*sizeof(struct page_table_entry **));
                    if(new_page_table[i][j] == NULL) {
                        return ENOMEM;
                    }
                    for(int k=0; k < 256; k++) {
                        if(old_page_table[i][j][k] != NULL) {
                            new_page_table[i][j][k] = kmalloc(256*sizeof(struct page_table_entry *));
                            if(new_page_table[i][j][k] == NULL) {
                                return ENOMEM;
                            }
                            for(int l=0; l < 256; l++) {
                                if(old_page_table[i][j][k][l] != NULL) {
                                    new_page_table[i][j][k][l] = kmalloc(sizeof(struct page_table_entry));
                                    if(new_page_table[i][j][k][l] == NULL) {
                                        return ENOMEM;
                                    }
                                    lock_acquire(old_page_table[i][j][k][l]->pte_lock);
                                    new_page_table[i][j][k][l]->vaddr = old_page_table[i][j][k][l]->vaddr;
                                    new_page_table[i][j][k][l]->swap_index = -1;
                                    new_page_table[i][j][k][l]->on_disk = false;
                                    if(old_page_table[i][j][k][l]->paddr != 0) {
                                  	lock_acquire(lock_copy);
					memmove(kbuf,(const void *)PADDR_TO_KVADDR(old_page_table[i][j][k][l]->paddr),PAGE_SIZE);
                                        new_page_table[i][j][k][l]->paddr = page_alloc(1,new_page_table[i][j][k][l]->vaddr,new_page_table[i][j][k][l]);
                                        if(new_page_table[i][j][k][l]->paddr == 0) {
                                            lock_release(lock_copy);
                                            lock_release(old_page_table[i][j][k][l]->pte_lock);
                                            return ENOMEM;
                                        }
                                        memmove((void *)PADDR_TO_KVADDR(new_page_table[i][j][k][l]->paddr),kbuf,PAGE_SIZE);
                                        lock_release(lock_copy);
                                    }
                                    else {
                                        new_page_table[i][j][k][l]->paddr = 0;
                                        lock_acquire(lock_copy);
                                        struct iovec iov;
                                        struct uio kuio;
                                        uio_kinit(&iov,&kuio,kbuf,PAGE_SIZE,old_page_table[i][j][k][l]->swap_index*PAGE_SIZE,UIO_READ);
                                        int err = VOP_READ(swap_disk,&kuio);
                                        if(err) {
                                            lock_release(lock_copy);
                                            lock_release(old_page_table[i][j][k][l]->pte_lock);
                                            return err;
                                        }
                                        lock_acquire(swap_lock);
                                        for(int i=0;i< MAX_SWAP; i++) {
                                            if(swapmap[i] == false) {
                                                new_page_table[i][j][k][l]->swap_index = i;
                                                swapmap[i] = true;
                                            }
                                        }
                                        lock_release(swap_lock);
                                        uio_kinit(&iov,&kuio,kbuf,PAGE_SIZE,new_page_table[i][j][k][l]->swap_index*PAGE_SIZE,UIO_WRITE);
                                        err = VOP_READ(swap_disk,&kuio);
                                        if(err) {
                                            lock_release(lock_copy);
                                            lock_release(old_page_table[i][j][k][l]->pte_lock);
                                            return err;
                                        }
                                        lock_release(lock_copy);
                                    }
                                    new_page_table[i][j][k][l]->pte_lock = lock_create("pl");
                                    if(new_page_table[i][j][k][l]->pte_lock == NULL) {
                                        lock_release(old_page_table[i][j][k][l]->pte_lock);
                                        return ENOMEM;
                                    }
				    new_page_table[i][j][k][l]->pte_cv = cv_create("pc");
                                    if(new_page_table[i][j][k][l]->pte_cv == NULL) { //destroy on error
                                        lock_release(old_page_table[i][j][k][l]->pte_lock);
                                        return ENOMEM;
                                    }
                                    lock_release(old_page_table[i][j][k][l]->pte_lock);
                                }
                                else {
                                    new_page_table[i][j][k][l] = NULL;
                                }
                            }
                        }
                        else {
                            new_page_table[i][j][k] = NULL;
                        }
                    }
                }
                else {
                    new_page_table[i][j] = NULL;
                }
            }
        }
        else {
            new_page_table[i] = NULL;
        }
    }

    return 0;
//  linked list implementation

/*    struct page_table_entry *t_oldpte = old_as->page_table;

    if(t_oldpte == NULL) {
        new_as->page_table = NULL;
        return 0;
    }

    bool head = false;
    struct page_table_entry *t_newpte = kmalloc(sizeof(struct page_table_entry));
    if(t_newpte == NULL) {
        return ENOMEM;
    }

    do {
        if(head == false) {
            head = true;
            t_newpte->vaddr = t_oldpte->vaddr;
            t_newpte->paddr = 0;
            t_newpte->next = NULL;
            new_as->page_table = t_newpte;
            t_oldpte = t_oldpte->next;
        }
        else {
            struct page_table_entry *temp = kmalloc(sizeof(struct page_table_entry));
            if(temp == NULL) {
                free_page_table(new_as->page_table);
                return ENOMEM;
            }
            temp->vaddr = t_oldpte->vaddr;
            temp->paddr = t_oldpte->paddr;
            temp->next = NULL;
            t_newpte->next = temp;
            t_newpte = temp;
            t_oldpte = t_oldpte->next;
        }
    } while(t_oldpte);

    //(void)as;
    return 0;*/
}

void
free_list(struct region_entry **reg_list) {
    if(reg_list == NULL) {
        return;
    }
    struct region_entry *t_reg = *reg_list;
    while(t_reg != NULL) {
        struct region_entry *temp = t_reg->next;
        kfree(t_reg);
        t_reg = temp;
    }
    *reg_list = NULL;
    return;
}


int
copy_regions(struct addrspace *old_as, struct region_entry **newreg)
{


    if(old_as->regions == NULL) {
        *newreg = NULL;
        return 0;
    }

    struct region_entry *t_oldreg = old_as->regions;
    struct region_entry *t_newreg = NULL;
    struct region_entry *head = NULL;

    do {
        if(t_newreg == NULL) {
            struct region_entry *temp = kmalloc(sizeof(struct region_entry));
            if(temp == NULL) {
                return ENOMEM;
            }
            temp->reg_base = t_oldreg->reg_base;
            temp->bounds = t_oldreg->bounds;
            temp->original_permissions = t_oldreg->original_permissions;
            temp->temp_permissions = t_oldreg->temp_permissions;
            temp->next = NULL;
            head = temp;
            t_newreg = temp;
            t_oldreg = t_oldreg->next;
        }
        else {
            struct region_entry *temp = kmalloc(sizeof(struct region_entry));
            if(temp == NULL) {
                free_list(&head);
                return ENOMEM;
            }
            temp->reg_base = t_oldreg->reg_base;
            temp->bounds = t_oldreg->bounds;
            temp->original_permissions = t_oldreg->original_permissions;
            temp->temp_permissions = t_oldreg->temp_permissions;
            temp->next = NULL;
            t_newreg->next = temp;
            t_newreg = temp;
            t_oldreg = t_oldreg->next;
        }
    } while(t_oldreg);

    *newreg = head;
    return 0;

}

void
free_page_table(struct page_table_entry ******page_table)
{
    if(*page_table == NULL) {
        return;
    }
    struct page_table_entry *****t_page_table = *page_table;

    for(int i=0; i < 256; i++) {
        if(t_page_table[i] != NULL) {
            for(int j=0; j<256; j++) {
                if(t_page_table[i][j] != NULL) {
                    for(int k=0; k<256; k++) {
                        if(t_page_table[i][j][k] != NULL) {
                            for(int l=0; l < 256; l++) {
                                if(t_page_table[i][j][k][l] != NULL) {
                                    lock_acquire(t_page_table[i][j][k][l]->pte_lock);
				    if(t_page_table[i][j][k][l]->on_disk) {
					swapmap[t_page_table[i][j][k][l]->swap_index] = false;
					goto nowait;
				    }
				    while(coremap[t_page_table[i][j][k][l]->paddr/4096].busy) {
					cv_wait(t_page_table[i][j][k][l]->pte_cv,t_page_table[i][j][k][l]->pte_lock);
				    }
				    nowait:
				    if(!t_page_table[i][j][k][l]->on_disk) {
					page_free(t_page_table[i][j][k][l]);
				    }
				    lock_release(t_page_table[i][j][k][l]->pte_lock);
				    lock_destroy(t_page_table[i][j][k][l]->pte_lock);
				    cv_destroy(t_page_table[i][j][k][l]->pte_cv);
                                    kfree(t_page_table[i][j][k][l]);
                                    t_page_table[i][j][k][l] = NULL;
                                }
                            }
                            kfree(t_page_table[i][j][k]);
                            t_page_table[i][j][k] = NULL;
                        }
                    }
                    kfree(t_page_table[i][j]);
                    t_page_table[i][j] = NULL;
                }
            }
            kfree(t_page_table[i]);
            t_page_table[i] = NULL;
        }
    }
    kfree(t_page_table);
    *page_table = NULL;
    return;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

    as->heap_start = 0;
    as->heap_end = 0;
    as->regions = NULL;
    as->page_table = kmalloc(256*sizeof(struct page_table_entry ****));
    if(as->page_table == NULL) {
         return NULL;
    }
    for(int i=0;i<256;i++) {
         as->page_table[i] = NULL;
    }
    //as->page_table = NULL;
    as->stack_end = USERSTACK;

	/*
	 * Initialize as needed.
	 */

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

    int err = copy_page_table(old, newas);
    if(err) {
        as_destroy(newas);
         return err;
    }

    err = copy_regions(old, &newas->regions);
    if(err) {
        as_destroy(newas);
        return err;
    }
    newas->heap_start = old->heap_start;
    newas->heap_end = old->heap_end;
    newas->stack_end = old->stack_end;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
    if(as) {
        free_page_table(&as->page_table);
        as->page_table = NULL;
        //free_list(as->page_table);
        free_list(&as->regions);
    }
	kfree(as);
    (void)as;
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

    int spl = splhigh();
    for(int i=0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */

    memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr = vaddr & PAGE_FRAME;
    memsize = (memsize + PAGE_SIZE -1) & PAGE_FRAME;
    struct region_entry *new_region = add_region(as, vaddr, memsize, readable, writeable, executable);
    if(new_region == NULL) {
         return ENOMEM;
    }
    as->heap_start = vaddr + memsize;
    as->heap_end = (as->heap_end > as->heap_start)?as->heap_end:as->heap_start;
    return 0;
}

int
as_prepare_load(struct addrspace *as)
{
    struct region_entry *t_region = as->regions;
    while(t_region) {
         t_region->original_permissions = AS_READABLE | AS_WRITEABLE;
         t_region = t_region->next;
    }
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
    struct region_entry *t_region = as->regions;
    while(t_region) {
         t_region->original_permissions = t_region->temp_permissions;
         t_region = t_region->next;
    }
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

struct page_table_entry*
get_pte(struct addrspace *as,const vaddr_t faultaddress) {
    /* 4-level page table implementation */
    int top_index = TOP_INDEX(faultaddress);
    if(as->page_table[top_index] == NULL)
        return NULL;

    int second_index = SECOND_INDEX(faultaddress);
    if(as->page_table[top_index][second_index] == NULL)
        return NULL;

    int third_index = THIRD_INDEX(faultaddress);
    if(as->page_table[top_index][second_index][third_index] == NULL)
        return NULL;

    int forth_index = FORTH_INDEX(faultaddress);
    if(as->page_table[top_index][second_index][third_index][forth_index] == NULL)
        return NULL;

    return as->page_table[top_index][second_index][third_index][forth_index];

    //linked list

    //vaddr = vaddr & PAGE_FRAME;
    /*
    struct page_table_entry *t_pte = as->page_table;
    while(t_pte) {
        if(t_pte->vaddr == vaddr) {
            return t_pte;
        }
        t_pte = t_pte->next;
    }
    return NULL;*/

}

struct page_table_entry*
add_pte(struct addrspace *as, vaddr_t vaddr,paddr_t paddr)
{
//    4-level page table implementation

    // top level
    int top_index = TOP_INDEX(vaddr);
    if(as->page_table[top_index] == NULL) {
        as->page_table[top_index] = kmalloc(256*sizeof(struct page_table_entry ***));
        if(as->page_table[top_index] == NULL) {
             return NULL;
        }
        for(int i=0;i<256;i++) {
            as->page_table[top_index][i] = NULL;
        }
    }

    //second level
    int second_index = SECOND_INDEX(vaddr);
    if(as->page_table[top_index][second_index] == NULL) {
        as->page_table[top_index][second_index] = kmalloc(256*sizeof(struct page_table_entry **));
        if(as->page_table[top_index] == NULL) {
             return NULL;
        }
        for(int i=0;i<256;i++) {
            as->page_table[top_index][second_index][i] = NULL;
        }
    }

    //third level
    int third_index = THIRD_INDEX(vaddr);
    if(as->page_table[top_index][second_index][third_index] == NULL) {
        as->page_table[top_index][second_index][third_index] = kmalloc(256*sizeof(struct page_table_entry *));
        if(as->page_table[top_index] == NULL) {
             return NULL;
        }
        for(int i=0;i<256;i++) {
            as->page_table[top_index][second_index][third_index][i] = NULL;
        }

    }

    //forth_level
    int forth_index = FORTH_INDEX(vaddr);

    struct page_table_entry *temp = kmalloc(sizeof(struct page_table_entry));
    if(temp == NULL) {
        return NULL;
    }

    temp->vaddr = vaddr & PAGE_FRAME;
    temp->paddr = paddr;
    temp->on_disk = false;
    temp->swap_index = -1;
    temp->pte_lock = lock_create("pl");
    if(temp->pte_lock == NULL) {
	    return NULL;
    }
    temp->pte_cv = cv_create("pc");
    if(temp->pte_cv == NULL) {
	    return NULL;
    }

    as->page_table[top_index][second_index][third_index][forth_index] = temp;

    return temp;
//    linked list implementation
/*    struct page_table_entry *temp = kmalloc(sizeof(struct page_table_entry));
    if(temp == NULL) {
        return NULL;
    }
    temp->vaddr = vaddr & PAGE_FRAME;
    temp->paddr = paddr;
    temp->next = NULL;
    if(as->page_table == NULL) {
        as->page_table = temp;
        return temp;
    }

    struct page_table_entry *t_pte = as->page_table;
    while(t_pte->next) {
        t_pte = t_pte->next;
    }
    t_pte->next = temp;

    return temp;*/
}

struct region_entry *
add_region(struct addrspace *as, vaddr_t base, size_t memsize, int r, int w, int e)
{
    struct region_entry *temp = kmalloc(sizeof(struct region_entry));
    if(temp == NULL) {
        return NULL;
    }
    temp->reg_base = base & PAGE_FRAME;
    temp->bounds = memsize;
    temp->original_permissions = r | w | e;
    temp->temp_permissions = r | w | e;
    temp->next = NULL;

    if(as->regions == NULL) {
        as->regions = temp;
        return temp;
    }
    struct region_entry *t_reg = as->regions;
    while(t_reg->next) {
        t_reg = t_reg->next;
    }
    t_reg->next = temp;

    return temp;
}

void
free_pte(struct addrspace *as, vaddr_t vaddr)
{
    int top_index = TOP_INDEX(vaddr);
    if(as->page_table[top_index] == NULL)
        return;

    int second_index = SECOND_INDEX(vaddr);
    if(as->page_table[top_index][second_index] == NULL)
        return;

    int third_index = THIRD_INDEX(vaddr);
    if(as->page_table[top_index][second_index][third_index] == NULL)
        return;

    int forth_index = FORTH_INDEX(vaddr);
    if(as->page_table[top_index][second_index][third_index][forth_index] == NULL)
        return;

    page_free(as->page_table[top_index][second_index][third_index][forth_index]);
    as->page_table[top_index][second_index][third_index][forth_index]->paddr = 0;
    as->page_table[top_index][second_index][third_index][forth_index]->vaddr = 0;
    kfree(as->page_table[top_index][second_index][third_index][forth_index]);
    as->page_table[top_index][second_index][third_index][forth_index] = NULL;

}

void
free_pages(struct addrspace *as, vaddr_t start_addr, vaddr_t end_addr)
{
    for(unsigned i = (start_addr & PAGE_FRAME); i < (end_addr & PAGE_FRAME); i+=PAGE_SIZE) {
        free_pte(as,i);
        /*struct page_table_entry *pte = get_pte(as, i);
        if(pte == NULL) {
            continue;
        }
        page_free(pte);
        pte->paddr = 0;
        pte->vaddr = 0;
        kfree(pte);*/
    }
}
