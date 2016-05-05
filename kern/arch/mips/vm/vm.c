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
struct lock *lock_copy;
struct semaphore *sem_tlb;
int swap_top = 0;
struct lock *disk_lock;
struct spinlock splk_tlb;

bool check_if_valid(vaddr_t vaddr, struct addrspace *as, int *permission);

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
            en.fixed = true;
            en.free = false;
        }
        else {
            en.free = true;
            en.fixed = false;
        }
        en.recent = false;
        en.block_size = 0;
        en.cpu = -1;
        en.busy = false;
        en.dirty = false;
        en.pte = NULL;
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

    swap_enable = false;
    disk_lock = lock_create("dl");
    swapmap = bitmap_create(MAX_SWAP);
    lock_swap = lock_create("lock_swap");
    sem_tlb = sem_create("sem_tlb",0);
    lock_copy = lock_create("lock_copy");
    spinlock_init(&splk_tlb);
}

paddr_t
page_alloc(unsigned npages,struct page_table_entry *pte)
{
    int start_index = -1;
    int n_swap_pages = 0;

    paddr_t pa;
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
            if(coremap[i].busy != true || coremap[i].fixed == false) {
                if(coremap[i].recent == false) {
                    bool available = true;
                    for(unsigned j = i; j < i + npages; j++) {
                        if(coremap[j].busy == true || coremap[j].fixed == true) {
                                available = false;
                                break;
                        }
                    }
                    if(available) {
                        start_index = i;
                        pa = i*PAGE_SIZE;
                        for(unsigned j = i; j < i + npages; j++) {
                            KASSERT(coremap[j].fixed != true);
                            coremap[j].busy = true;
                        }
                        break;
                    }
                }
                else {
                    coremap[i].recent = false;
                }
            }
        }
        else {
            if(coremap[i].free == true) {
                bool available = true;
                for(unsigned j = i; j < i + npages; j++) {
                    if(coremap[j].fixed == true
                            || coremap[j].busy == true) {
                        available = false;
                        break;
                    }
                }
                if(available) {
                    start_index = i;
                    pa = i*PAGE_SIZE;
                    for(unsigned j = i; j < i + npages; j++) {
                        KASSERT(coremap[j].fixed != true);
                        coremap[j].busy = true;
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
        if(swap_enable && coremap[j].free != true) {
            int err = page_evict(j);
            if(err) {
                //cleanup already assigned pages in this block;
                return 0;
            }
        }
        coremap[j].recent = false;
        coremap[j].free = false;
        coremap[j].fixed = (pte == NULL)?true:false;
        coremap[j].block_size = npages;
        coremap[j].pte = pte;
    }

    bzero((void *)PADDR_TO_KVADDR(pa), npages*PAGE_SIZE);

    return pa;
}

void
page_free(struct page_table_entry *pte)
{
    KASSERT(pte->on_disk == false);
    KASSERT(pte->paddr != 0);

    spinlock_acquire(&splk_coremap);
    unsigned index = pte->paddr/PAGE_SIZE;
    coremap[index].free = true;
    coremap[index].busy = false;
    coremap[index].block_size = 0;
    coremap[index].pte= NULL;
    coremap[index].recent = false;
    coremap[index].dirty = false;
    num_allocated_pages--;
    spinlock_release(&splk_coremap);


}

vaddr_t
alloc_kpages(unsigned npages)
{
    paddr_t pa;
    pa = page_alloc(npages,NULL);
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
        coremap[i].free = true;
        coremap[i].fixed = false;
        coremap[i].busy = false;
        coremap[i].dirty = false;
        coremap[i].block_size = 0;
        coremap[i].recent = false;
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
    spinlock_acquire(&splk_tlb);
    int index = tlb_probe(ts->ts_vaddr,0);
    if(index > 0) {
        tlb_write(TLBHI_INVALID(index),TLBLO_INVALID(),index);
    }
    spinlock_release(&splk_tlb);
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
    int permission;
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
    bool new_page = false;
    if(pte == NULL) {
        pte = add_pte(as,faultaddress,0);
        if(pte == NULL) {
            return ENOMEM;
        }
    }
    if(swap_enable) {    lock_acquire(pte->pte_lock); }

    if(pte->on_disk || pte->paddr == 0) {
        pte->paddr = page_alloc(1,pte);
        new_page = true;
        KASSERT(coremap[pte->paddr/4096].busy = true);
        if(pte->on_disk == true) {
            int err = swap_in(pte);
            if (err) {
                return err;
            }
            else if(pte->paddr == 0) {
                return ENOMEM;
            }
        }
    }

    int spl = splhigh();
    uint32_t ehi = faultaddress;
    uint32_t elo = pte->paddr | TLBLO_VALID;

    if((permission & AS_WRITEABLE) == AS_WRITEABLE) {
        elo = pte->paddr | TLBLO_DIRTY | TLBLO_VALID;
    }

    int index = tlb_probe(ehi,0);
    if(index > 0) {
        tlb_write(ehi,elo,index);
    }
    else {
        tlb_random(ehi,elo);
    }
    splx(spl);

    coremap[pte->paddr/PAGE_SIZE].cpu = curcpu->c_number;
    if((permission & AS_WRITEABLE) == AS_WRITEABLE && new_page == true) {
        coremap[pte->paddr/PAGE_SIZE].dirty = true;
        coremap[pte->paddr/PAGE_SIZE].busy = false;
    }
    else if(new_page == true){
        coremap[pte->paddr/PAGE_SIZE].dirty = true;
        coremap[pte->paddr/PAGE_SIZE].busy = false;
    }

    coremap[pte->paddr/PAGE_SIZE].recent = true;

    if(swap_enable) {
        lock_release(pte->pte_lock);
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


void
debug_vm(void)
{
/*    int nfree = 0;
    kprintf("\nleaking pages: ");
    for(unsigned i=num_fixed+1;i<num_total_pages;i++) {
        if(coremap[i].page_state != PS_FREE) {
            kprintf("%u ",i);
        }
	else {
		nfree++;
	}
    }

    kprintf("\n ");
    for(unsigned i = 0; i <= num_fixed; i++) {
        if(coremap[i].page_state == PS_FREE) {
            kprintf("%u ",i);
        }
    }
*/

}

int
page_evict(unsigned index)
{
    KASSERT(swap_enable == true);
    KASSERT(coremap[index].busy == true);
    KASSERT(coremap[index].pte != NULL);
    //lock the page and change the state to locked and shootdown tlb entry
    struct page_table_entry *evict_pte = coremap[index].pte;
    lock_acquire(evict_pte->pte_lock);

    struct tlbshootdown ts;
    ts.ts_vaddr = evict_pte->vaddr;
    ipi_tlbshootdown(get_cpu(coremap[index].cpu),&ts);

    //write the page to disk if the page_state is dirty
    //if(page_state == PS_DIRTY) {
        int err = swap_out(evict_pte);
        if(err) {
            lock_release(evict_pte->pte_lock);
            return err;
        }
    //}
    evict_pte->on_disk = true;
    lock_release(evict_pte->pte_lock);
    return 0;
}

int
swap_out(struct page_table_entry *pte)
{

    //lock_acquire(disk_lock);
    struct iovec iov;
    struct uio kuio;

    uio_kinit(&iov, &kuio, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE, pte->swap_index*PAGE_SIZE, UIO_WRITE);
    int err = VOP_WRITE(swap_disk,&kuio);
    if(err) {
        return err;
    }
    pte->on_disk = true;
    //lock_release(disk_lock);
    return 0;
}

int
swap_in(struct page_table_entry *pte)
{
    //lock_acquire(disk_lock);
    struct iovec iov;
    struct uio kuio;

    uio_kinit(&iov, &kuio, (void *)PADDR_TO_KVADDR(pte->paddr), PAGE_SIZE, pte->swap_index*PAGE_SIZE, UIO_READ);
    int err = VOP_READ(swap_disk, &kuio);
    if(err) {
        return err;
    }
    pte->on_disk = false;
    //lock_release(disk_lock);
    return 0;
}
