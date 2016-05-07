/* created by Miraj on 25h March */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <bitmap.h>
#include <thread.h>
#include <synch.h>
#include <kern/fcntl.h>
#include <vnode.h>
#include <uio.h>
#include <proc.h>
#include <stat.h>
#include <current.h>
#include <mips/tlb.h>
#include <vfs.h>
#include <addrspace.h>
#include <vm.h>

struct spinlock splk_coremap;
unsigned num_allocated_pages = 0;
unsigned num_total_pages = 0;
struct coremap_entry *coremap;
unsigned num_fixed = 0;
bool vm_bootstrapped = false;
static unsigned search_start = 0;
struct lock *lock_copy;
struct spinlock lock_swap;
int swap_top = 0;
bool swap_enable = false;
struct vnode *swap_disk;
struct bitmap *swapmap;
struct semaphore *sem_tlb;

bool check_if_valid(vaddr_t vaddr, struct addrspace *as,int *permission);

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
        en.busy = false;
        //en.recent = false;
        en.block_size = 0;
        en.cpu = -1;
        en.pte = NULL;
        coremap[i] = en;
    }
    vm_bootstrapped = true;

}

void
swap_bootstrap()
{
    int err = vfs_open((char *)"lhd0raw:",O_RDWR,0664,&swap_disk);
    if(err) {
        swap_enable = false;
        return;
    }
    else {
        swap_enable = true;
    }

    struct stat f_stat;
    VOP_STAT(swap_disk,&f_stat);
    lock_copy = lock_create("lc");
    spinlock_init(&lock_swap);
    swapmap = bitmap_create(f_stat.st_size/PAGE_SIZE);
    if(swapmap == NULL) {
         panic("asdasd:");
    }
    sem_tlb = sem_create("st",0);
}

vaddr_t
alloc_kpages(unsigned npages)
{
    int start_index = -1;
    unsigned evict_states[npages];
    paddr_t pa;
    spinlock_acquire(&splk_coremap);
    if(swap_enable == false && num_allocated_pages + npages >= num_total_pages - num_fixed) {
        spinlock_release(&splk_coremap);
        return 0;
    }
    unsigned i = search_start;
    while(true) {
        if(i+npages-1 >= num_total_pages) {
            i = num_fixed + 1;
            continue;
        }
        if(swap_enable == true) {
            if((coremap[i].busy == false) && (coremap[i].page_state == PS_FREE || coremap[i].page_state == PS_CLEAN || coremap[i].page_state == PS_DIRTY)) {
                if(coremap[i].recent == false) {
                    bool available = true;
                    for(unsigned j = i; j < i + npages; j++) {
                        if(coremap[j].page_state == PS_FIXED
                            || coremap[j].page_state == PS_VICTIM
                            || coremap[j].busy == true) {
                            available = false;
                            break;
                        }
                    }
                    if(available) {
                        start_index = i;
                        pa = i*PAGE_SIZE;
                        for(unsigned j = i; j < i + npages; j++) {
                            KASSERT(coremap[i].page_state != PS_FIXED);
                            evict_states[j-i] = coremap[j].page_state;
                            if(evict_states[j-i] != PS_FREE) {
                                 num_allocated_pages--;
                            }
                            coremap[j].page_state = PS_VICTIM;
                            coremap[j].busy = true;
                        }
                        break;
                    }
                }
                else {
                    coremap[i].recent = false;
                }
            }
            i++;
        }
        else if(coremap[i].page_state == PS_FREE) { //|| coremap[i].page_state == PS_CLEAN || coremap[i].page_state == PS_DIRTY) {
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
                    evict_states[j-i] = coremap[j].page_state;
                    coremap[j].page_state = PS_VICTIM;
                }
                break;
            }
        }
        i++;
    }
    pa = start_index * PAGE_SIZE;
    search_start = start_index + npages;
    num_allocated_pages += npages;

    spinlock_release(&splk_coremap);
    for(unsigned j = start_index; j < start_index + npages; j++) {
        if(swap_enable && evict_states[j-start_index] != PS_FREE) {
            lock_acquire(coremap[j].pte->p_lock);
            struct tlbshootdown ts;
            ts.ts_vaddr = coremap[j].pte->vaddr;
            struct cpu* c = get_cpu(coremap[j].cpu);
            ipi_tlbshootdown(c,&ts);
            P(sem_tlb);
            if(coremap[j].page_state == PS_DIRTY) {
                evict_states[j-start_index] = PS_DIRTY;
            }
            if(evict_states[j-start_index] == PS_DIRTY) {
                struct uio kuio;
                struct iovec iov;
                uio_kinit(&iov,&kuio,(void *)PADDR_TO_KVADDR(coremap[j].pte->paddr),PAGE_SIZE,coremap[j].pte->swap_index*PAGE_SIZE,UIO_WRITE);
                if(coremap[j].pte->swap_index != coremap[j].pte->dup) {
                    panic("si changed");
                }
                int err = VOP_WRITE(swap_disk,&kuio);
                if(err) {
                    panic("asdasdas");
                }
            }
            coremap[j].pte->on_disk = true;
            coremap[j].page_state = PS_FIXED;
            coremap[j].busy = false;
            coremap[j].block_size = npages;

            bzero((void *)PADDR_TO_KVADDR(j*PAGE_SIZE),PAGE_SIZE);
            lock_release(coremap[j].pte->p_lock);
            coremap[j].pte = NULL;
        }
        else {
            coremap[j].page_state = PS_FIXED;
            coremap[j].busy = false;
            coremap[j].block_size = npages;
            coremap[j].pte = NULL;
            bzero((void *)PADDR_TO_KVADDR(j*PAGE_SIZE),PAGE_SIZE);
        }
    }

    return PADDR_TO_KVADDR(pa);
}

void
page_free(paddr_t paddr)
{
    spinlock_acquire(&splk_coremap);
    unsigned index = paddr/PAGE_SIZE;
    coremap[index].page_state = PS_FREE;
    coremap[index].block_size = 0;
    coremap[index].pte= NULL;
    coremap[index].busy = false;
    coremap[index].cpu = -1;
    num_allocated_pages--;
    spinlock_release(&splk_coremap);
}

void
free_kpages(vaddr_t addr)
{
    if(vm_bootstrapped) {
        spinlock_acquire(&splk_coremap);
        paddr_t pa = KVADDR_TO_PADDR(addr);
        unsigned index = pa/PAGE_SIZE;
        unsigned block_size = coremap[index].block_size;
        num_allocated_pages -= block_size;
        for(unsigned i = index; i < index + block_size; i++) {
            coremap[i].page_state = PS_FREE;
            coremap[i].block_size = 0;
            coremap[i].busy = false;
            coremap[i].cpu = -1;
        }
        spinlock_release(&splk_coremap);
    }
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
    int spl = splhigh();
    int index = tlb_probe(ts->ts_vaddr,0);
    if(index > 0) {
        tlb_write(TLBHI_INVALID(index),TLBLO_INVALID(),index);
    }
    V(sem_tlb);
    splx(spl);
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
    int permission = AS_WRITEABLE;
    bool valid = check_if_valid(faultaddress,as,&permission);
    if(!valid) {
        return EFAULT;
    }

    switch(faulttype) {
        case VM_FAULT_READONLY:
            if(!((permission & AS_WRITEABLE) == AS_WRITEABLE)) {
                return EFAULT;
            }
            break;
        case VM_FAULT_READ:
            if(!((permission & AS_READABLE) == AS_READABLE)) {
                return EFAULT;
            }
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
    //bool new_page = false;
    struct page_table_entry *pte = get_pte(as,faultaddress);
    if(pte == NULL) {
        pte = add_pte(as,faultaddress,0);
        pte->paddr = alloc_upages(pte);
        if(pte == NULL) {
            return ENOMEM;
        }
    //    new_page = true;
    }
    if(swap_enable == true) {
        lock_acquire(pte->p_lock);
     //   if(new_page == false && pte->on_disk == false) {
     //       int index = pte->paddr/PAGE_SIZE;
     //       while(coremap[index].page_state == PS_VICTIM) {
     //           lock_release(pte->p_lock);
     //           thread_yield();
     //           lock_acquire(pte->p_lock);
     //       }
     //   }
    }
    if(pte->on_disk == true) {
        pte->paddr = alloc_upages(pte);
        if(pte->on_disk == true) {
            struct uio kuio;
            struct iovec iov;
            uio_kinit(&iov,&kuio,(void *)PADDR_TO_KVADDR(pte->paddr),PAGE_SIZE,pte->swap_index*PAGE_SIZE,UIO_READ);
            KASSERT(bitmap_isset(swapmap,pte->swap_index) != false);
            if(pte->swap_index != pte->dup) {
                panic("si changed");
            }
            int err = VOP_READ(swap_disk,&kuio);
            if(err) {
                panic("disk fail");
            }
            pte->on_disk = false;
        }
        if(pte->paddr == 0) {
            return ENOMEM;
        }
        coremap[pte->paddr/PAGE_SIZE].page_state = PS_CLEAN;
    }

    int spl = splhigh();
    uint32_t ehi = faultaddress;
    uint32_t elo = pte->paddr | TLBLO_VALID;

    if(faulttype == VM_FAULT_READONLY || faulttype == VM_FAULT_WRITE) {
        coremap[pte->paddr/PAGE_SIZE].page_state = PS_DIRTY;
        elo = pte->paddr | TLBLO_DIRTY | TLBLO_VALID;
    }

    int index = tlb_probe(ehi,0);
    if(index > 0) {
        tlb_write(ehi,elo,index);
    }
    else {
        tlb_random(ehi,elo);
    }
    coremap[pte->paddr/4096].cpu = curcpu->c_number;
    coremap[pte->paddr/4096].recent = true;
    splx(spl);
    if(swap_enable == true) {
        lock_release(pte->p_lock);
    }

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

paddr_t
alloc_upages(struct page_table_entry *pte)
{
    int start_index = -1;
    paddr_t pa;
    unsigned evict_page_state;
    spinlock_acquire(&splk_coremap);
    if(swap_enable == false && num_allocated_pages + 1 >= num_total_pages - num_fixed) {
        spinlock_release(&splk_coremap);
        return 0;
    }
    unsigned i = search_start;
    while(true) {
        if(i >= num_total_pages) {
            i = num_fixed + 1;
            continue;
        }
        if(swap_enable == false) {
            if(coremap[i].page_state == PS_FREE) {
                start_index = i;
                pa = i*PAGE_SIZE;
                evict_page_state = coremap[i].page_state;
                coremap[i].page_state = PS_VICTIM;
                coremap[i].busy = true;
                break;
            }
        }
        else if((coremap[i].busy == false) && (coremap[i].page_state == PS_FREE || coremap[i].page_state == PS_CLEAN || coremap[i].page_state == PS_DIRTY)) {
            if(coremap[i].recent == false) {
                start_index = i;
                pa = i*PAGE_SIZE;
                KASSERT(coremap[i].page_state != PS_FIXED);
                evict_page_state = coremap[i].page_state;
                coremap[i].page_state = PS_VICTIM;
                coremap[i].busy = true;
                if(evict_page_state != PS_FREE) {
                    num_allocated_pages--;
                }
                break;
            }
            else {
                coremap[i].recent = false;
            }
        }
        i++;
    }
    pa = start_index * PAGE_SIZE;
    int j = start_index;
    search_start = start_index + 1;
    num_allocated_pages += 1;
    spinlock_release(&splk_coremap);

    if(swap_enable && evict_page_state != PS_FREE) {
        KASSERT(coremap[j].busy == true);
        KASSERT(coremap[j].page_state == PS_VICTIM);
        lock_acquire(coremap[j].pte->p_lock);
        struct tlbshootdown ts;
        ts.ts_vaddr = coremap[j].pte->vaddr;
        struct cpu* c = get_cpu(coremap[j].cpu);
        ipi_tlbshootdown(c,&ts);
        P(sem_tlb);
        if(coremap[j].page_state == PS_DIRTY) {
            evict_page_state = PS_DIRTY;
        }

        if(evict_page_state == PS_DIRTY) {
            struct uio kuio;
            struct iovec iov;
            uio_kinit(&iov,&kuio,(void *)PADDR_TO_KVADDR(coremap[j].pte->paddr),PAGE_SIZE,coremap[j].pte->swap_index*PAGE_SIZE,UIO_WRITE);
            if(coremap[j].pte->swap_index != coremap[j].pte->dup) {
                panic("si changed");
            }
            int err = VOP_WRITE(swap_disk,&kuio);
            if(err) {
                panic("disk fail");
            }
        }

        coremap[j].pte->on_disk = true;
        coremap[j].page_state = PS_VICTIM;
        coremap[j].busy = false;
        coremap[j].block_size = 1;

        bzero((void *)PADDR_TO_KVADDR(pa), PAGE_SIZE);
        lock_release(coremap[j].pte->p_lock);
        coremap[j].pte = pte;
        KASSERT(pte != NULL);
    }
    else {
        coremap[j].block_size = 1;
        coremap[j].busy = false;
        coremap[j].page_state = PS_VICTIM;
        coremap[j].pte = pte;
        bzero((void *)PADDR_TO_KVADDR(pa), PAGE_SIZE);
    }

    return pa;
}

void debug_vm(){}

void check_coremap(int index,int swapslot){
    (void)index;
    (void)swapslot;
}
