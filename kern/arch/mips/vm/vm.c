/* created by Miraj on 25h March */

#include <types.h>
#include <limits.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <bitmap.h>
#include <current.h>
#include <mips/tlb.h>
#include <synch.h>
#include <addrspace.h>
#include <vm.h>
#include <vnode.h>
#include <vfs.h>
#include <uio.h>

struct spinlock splk_coremap;
unsigned num_allocated_pages = 0;
unsigned num_total_pages = 0;
struct coremap_entry *coremap;
unsigned num_fixed = 0;
bool vm_bootstrapped = false;
static unsigned search_start = 0;
struct vnode *swap_disk;
bool swap_enable = false;
struct bitmap *swapmap;
//bool swapmap[MAX_SWAP];
struct lock *lock_swap;
struct lock *lock_pte;
struct lock *lock_copy;
struct cv *cv_pte;
struct semaphore *sem_tlb;
int num_swap = 0;

void
vm_bootstrap()
{
    spinlock_init(&splk_coremap);
    paddr_t firstaddr, lastaddr, freeaddr;
    lastaddr = ram_getsize();
    num_total_pages = (lastaddr - lastaddr % PAGE_SIZE)/PAGE_SIZE;
    firstaddr = ram_getfirstfree();
    coremap = (struct coremap_entry*)PADDR_TO_KVADDR(firstaddr);
    freeaddr = firstaddr + num_total_pages * sizeof(struct coremap_entry);
    freeaddr = freeaddr + freeaddr%PAGE_SIZE;
    num_fixed = freeaddr/PAGE_SIZE;
    search_start = num_fixed + 1;
    for(unsigned i = 0; i < num_total_pages; i++) {
        struct coremap_entry en;
        if(i <= num_fixed) {
            en.page_state = PS_FIXED;
        }
        else {
            en.page_state = PS_FREE;
        }
        en.recent = false;
        en.block_size = 0;
        en.vaddr = 0;
	    en.cpu = -1;
        en.as = NULL;
        coremap[i] = en;
    }
    vm_bootstrapped = true;

}

void
swap_bootstrap()
{
    int err = vfs_open((char *)"lhd0raw:",O_RDWR,0,&swap_disk);
    if(err) {
        swap_enable = false;
        return;
    }
    else {
        swap_enable = true;
    }

    swapmap = bitmap_create(MAX_SWAP);
    lock_swap = lock_create("lock_swap");
    lock_pte = lock_create("lock_pte");
    cv_pte = cv_create("cv_pte");
    sem_tlb = sem_create("sem_tlb",0);
    lock_copy = lock_create("lock_copy");
}

paddr_t
page_alloc(unsigned npages, vaddr_t vaddr,struct addrspace *as)
{
    int start_index = -1;
    int n_swap_pages = 0;

    paddr_t pa;
    int evict_page_state[npages];
    spinlock_acquire(&splk_coremap);
    if(!swap_enable && num_allocated_pages + npages >= num_total_pages - num_fixed) {
        spinlock_release(&splk_coremap);
        return 0;
    }
    unsigned i = search_start;
    while(true) {
        if(i+npages-1 >= num_total_pages) {
            i = num_fixed + 1;
            continue;
        }
        if(swap_enable) {
            if((coremap[i].page_state == PS_FREE ||
                coremap[i].page_state == PS_CLEAN ||
                coremap[i].page_state == PS_DIRTY)) {
                if(true) {//coremap[i].recent == false) {
                    bool available = true;
                    for(unsigned j = i; j < i + npages; j++) {
                        if(coremap[j].page_state == PS_FIXED
                            || coremap[j].page_state == PS_VICTIM) {
                            available = false;
                            break;
                        }
                    }
                    if(available) {
                        start_index = i;
                        pa = i*PAGE_SIZE;
                        if(coremap[i].page_state != PS_FREE)
                            n_swap_pages++;
                        for(unsigned j = i; j < i + npages; j++) {
                            evict_page_state[j-i] = coremap[j].page_state;
                            coremap[j].page_state = PS_VICTIM;
                        }
                        break;
                    }
                }
                //else {
                //    coremap[i].recent = false;
                //}
            }
        }
        else {
            if(coremap[i].page_state == PS_FREE) {
                bool available = true;
                for(unsigned j = i; j < i + npages; j++) {
                    if(coremap[j].page_state == PS_FIXED
                            || coremap[j].page_state == PS_VICTIM) {
                        available = false;
                        break;
                    }
                }
                if(available) {
                    start_index = i;
                    pa = i*PAGE_SIZE;
                    for(unsigned j = i; j < i + npages; j++) {
                        coremap[j].page_state = PS_VICTIM;
                    }
                    break;
                }
            }
        }
        i++;
    }
    pa = start_index * PAGE_SIZE;
    search_start = start_index + npages;
    num_allocated_pages += npages - n_swap_pages;
    spinlock_release(&splk_coremap);

    for(unsigned j = start_index; j < start_index + npages; j++) {
        if(swap_enable) {
            int err = page_evict(j,evict_page_state[j-start_index]);
            if(err) {
                //cleanup already assigned pages in this block;
                return 0;
            }
        }
        //coremap[j].recent = (as==NULL)?true:false;
        coremap[j].page_state = (as == NULL)?PS_FIXED:PS_VICTIM;
        coremap[j].block_size = npages;
        coremap[j].vaddr = (as==NULL)?PADDR_TO_KVADDR(pa):(vaddr & PAGE_FRAME);
        coremap[j].as = as;
    }

    bzero((void *)PADDR_TO_KVADDR(pa), npages*PAGE_SIZE);

    return pa;
}

void
page_free(struct page_table_entry *pte)
{
    spinlock_acquire(&splk_coremap);
    unsigned index = pte->paddr/PAGE_SIZE;
    coremap[index].page_state = PS_FREE;
    coremap[index].block_size = 0;
    coremap[index].vaddr = 0;
    coremap[index].as = NULL;
    //coremap[index].recent = false;

    //if page is on the disk, change the status of swapmap for the page to unused
    if(pte->on_disk) {
	if(bitmap_isset(swapmap,pte->swap_index)) {
       		bitmap_unmark(swapmap,pte->swap_index);
	}
	//swapmap[pte->swap_index] = false;
    }
    else {
        num_allocated_pages--;
    }
    spinlock_release(&splk_coremap);
}

vaddr_t
alloc_kpages(unsigned npages)
{
    paddr_t pa;
    pa = page_alloc(npages,0,NULL);
    if(pa == 0) {
        return 0;
    }
    return PADDR_TO_KVADDR(pa);
}



void
free_kpages(vaddr_t addr)
{
    spinlock_acquire(&splk_coremap);
    paddr_t pa = KVADDR_TO_PADDR(addr);
    unsigned index = pa/PAGE_SIZE;
    unsigned block_size = coremap[index].block_size;
    num_allocated_pages -= block_size;
    for(unsigned i = index; i < index + block_size; i++) {
        coremap[i].page_state = PS_FREE;
        coremap[i].block_size = 0;
        //coremap[i].recent = false;
        coremap[i].vaddr = 0;
    }
    spinlock_release(&splk_coremap);
}

unsigned int
coremap_used_bytes()
{
    return num_allocated_pages*PAGE_SIZE;
}

void
vm_tlbshootdown_all()
{
    return;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    //spinlock_acquire(&splk_tlb);
    int spl = splhigh();
    if(curproc->p_addrspace == ts->ts_as) {
        int index = tlb_probe(ts->ts_vaddr,0);
        if(index > 0) {
            tlb_write(TLBHI_INVALID(index),TLBLO_INVALID(),index);
        }
    }
    V(sem_tlb);
    splx(spl);
    //spinlock_release(&splk_tlb);
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    struct addrspace *as = curproc->p_addrspace;
    if(as == NULL) {
        return EFAULT;
    }

    faultaddress &= PAGE_FRAME;

    //check if valid address
    int permission;// = AS_WRITEABLE;
    bool valid = check_if_valid(faultaddress,as,&permission);
    if(!valid) {
        return EFAULT;
    }

    switch(faulttype) {
        case VM_FAULT_READONLY:
            if(!((permission & AS_WRITEABLE) == AS_WRITEABLE)){
                return EFAULT;
            }
            break;
        case VM_FAULT_READ:
            if(!((permission & AS_READABLE) == AS_READABLE)) {
                return EFAULT;
            }
            permission &= AS_READABLE;
            break;
        case VM_FAULT_WRITE:
            if(!((permission & AS_WRITEABLE) == AS_WRITEABLE)) {
                return EFAULT;
            }
            break;
        default:
            return EINVAL;
    }

    //check if page_fault
    struct page_table_entry *pte = get_pte(as,faultaddress);


    if(pte == NULL) {
        pte = add_pte(as,faultaddress,0);
        if(pte == NULL) {
            return ENOMEM;
        }
    }

    lock_acquire(lock_pte);
    while(pte->locked) {
        cv_wait(cv_pte,lock_pte);
    }
    lock_release(lock_pte);

    /* wait if swapping is going on */

    if(pte->paddr == 0) {
        vaddr_t vaddr_temp = faultaddress;
        pte->paddr = page_alloc(1,vaddr_temp,as);
        if(pte->on_disk) {
            int err = swap_in(pte);
            if (err) {
                return err;
            }
            //pte->on_disk = false;
        }
        else if(pte->paddr == 0) {
            return ENOMEM;
        }
        coremap[pte->paddr/PAGE_SIZE].page_state = PS_CLEAN;
    }

    int spl = splhigh();
    uint32_t ehi = faultaddress;
    uint32_t elo = pte->paddr | TLBLO_VALID;

    if((permission & AS_WRITEABLE) == AS_WRITEABLE) {
        coremap[pte->paddr/PAGE_SIZE].page_state = PS_DIRTY;
        elo = pte->paddr | TLBLO_DIRTY | TLBLO_VALID;
    }
    //else {
    //    elo = pte->paddr | TLBLO_VALID;
    //}

    //coremap[pte->paddr/PAGE_SIZE].recent = true;
    coremap[pte->paddr/PAGE_SIZE].cpu = curcpu->c_number;
    int index = tlb_probe(ehi,0);
    if(index > 0) {
        tlb_write(ehi,elo,index);
    }
    else {
        tlb_random(ehi,elo);
    }
    splx(spl);

    return 0;
}

bool
check_if_valid(vaddr_t vaddr, struct addrspace *as,int *permission)
{
    struct region_entry *t_reg = as->regions;
    while(t_reg) {
        if(vaddr >= t_reg->reg_base && vaddr < t_reg->reg_base + t_reg->bounds) {
            *permission = t_reg->original_permissions;
            return true;
        }
        t_reg = t_reg->next;
    }
    if(vaddr >= as->heap_start && vaddr < as->heap_end) {
        *permission = AS_READABLE | AS_WRITEABLE;
        return true;
    }
    if(vaddr >= USERSTACKBASE && vaddr < USERSTACK) {
        *permission = AS_READABLE | AS_WRITEABLE;
        return true;
    }

    *permission = 0;
    return false;
}

void
debug_vm(void)
{
    int nfree = 0;
    kprintf("\nleaking pages: ");
    for(unsigned i=num_fixed+1;i<num_total_pages;i++) {
        if(coremap[i].page_state != PS_FREE) {
            kprintf("%u ",i);
        }
	else {
		nfree++;
	}
    }
/*
    kprintf("\n ");
    for(unsigned i = 0; i <= num_fixed; i++) {
        if(coremap[i].page_state == PS_FREE) {
            kprintf("%u ",i);
        }
    }
*/
}

int
page_evict(unsigned index, int page_state)
{
    KASSERT(swap_enable == true);

    /* if page is free, no need for swapping */
    if(page_state == PS_FREE) {
        return 0;
    }

    //lock the page and change the state to locked and shootdown tlb entry
    struct page_table_entry *evict_pte = get_pte(coremap[index].as,coremap[index].vaddr);
    lock_acquire(lock_pte);
    while(evict_pte->locked) {
        cv_wait(cv_pte,lock_pte);
    }
    evict_pte->locked = true;
    lock_release(lock_pte);

    //KASSERT(coremap[index].cpu >= 0);
    tlbshootdown(coremap[index].as,coremap[index].vaddr,coremap[index].cpu);

    //write the page to disk if the page_state is dirty
    //if(page_state == PS_DIRTY) {
        int err = swap_out(evict_pte);
        if(err) {
            return err;
        }
        evict_pte->on_disk = true;
    //}
    lock_acquire(lock_pte);
    evict_pte->locked = false;
    cv_broadcast(cv_pte,lock_pte);
    lock_release(lock_pte);

    return 0;
}

int
swap_out(struct page_table_entry *pte)
{

    lock_acquire(lock_swap);
    if(!pte->on_disk) {
        for(int i = 0; i < MAX_SWAP; i++) {
            if(!bitmap_isset(swapmap,i)) {
                if(!bitmap_isset(swapmap,i)) {
                    pte->swap_index = i;
                    bitmap_mark(swapmap,i);
                    break;
                }
            }
        }
        if(pte->swap_index == -1) {
            lock_release(lock_swap);
            return -1;
        }
    }
    lock_release(lock_swap);

    struct iovec iov;
    struct uio kuio;

    uio_kinit(&iov, &kuio, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE, pte->swap_index*PAGE_SIZE, UIO_WRITE);
    int err = VOP_WRITE(swap_disk,&kuio);
    if(err) {
        return err;
    }
    num_swap++;
    pte->paddr = 0;

    return 0;
}

int
swap_in(struct page_table_entry *pte)
{
    struct iovec iov;
    struct uio kuio;

    uio_kinit(&iov, &kuio, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE, pte->swap_index*PAGE_SIZE, UIO_READ);
    int err = VOP_READ(swap_disk, &kuio);
    if(err) {
        return err;
    }
   // num_swap--;
    bitmap_unmark(swapmap,pte->swap_index);
    //swapmap[pte->swap_index] = false;
    pte->swap_index = -1;
    pte->on_disk = false;
    return 0;
}
