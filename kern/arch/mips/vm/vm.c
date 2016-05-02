/* created by Miraj on 25h March */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

struct spinlock splk_coremap;
unsigned num_allocated_pages = 0;
unsigned num_total_pages = 0;
struct coremap_entry *coremap;
unsigned num_fixed = 0;
bool vm_bootstrapped = false;
static unsigned search_start = 0;
struct vnode *swap_disk;
bool swap_enable = false;
struct lock *lock_copy;
struct semaphore *sem_tlb;

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
        en.block_size = 0;
        en.vaddr = 0;
        en.as = NULL;
        coremap[i] = en;
    }
    vm_bootstrapped = true;

}

void
swap_bootstrap()
{
	int err = vfs_open((const chat *)"lhd0raw:",O_RDWR,0,&swap_disk);
	if(err) {
		swap_enable = false;
		return;
	}
	swap_enable = true;
	lock_copy = lock_create("lc");
	sem_tlb = sem_create("st",1);
}

paddr_t
page_alloc(unsigned npages, vaddr_t vaddr,struct addrspace *as)
{
    int start_index = -1;
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
		if(coremap[i].page_state == PS_FREE ||
			coremap[i].page_state == PS_DIRTY ||
			coremap[i],page_state == PS_CLEAN) {
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
					evict_page_state[j-i] = coremap[j].page_state;
					coremap[j].busy = true;
                    			coremap[j].page_state = PS_VICTIM;
                		}
                		break;		
			}	
		}
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
	if(swap_enable) {
		if(evict_page_state[j-start_index] == PS_FREE) {
			int err = evict_page(j);
			if(err) {
				return 0;
			}
		}
	}
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
	if(pte->on_disk) {
		swapmap[pte->swap_index] = false;
		return;
	}
        unsigned index = paddr/PAGE_SIZE;
        coremap[index].page_state = PS_FREE;
        coremap[index].block_size = 0;
        coremap[index].vaddr = 0;
        coremap[index].as = NULL;
        num_allocated_pages -= 1;
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
    if(vm_bootstrapped) {
        spinlock_acquire(&splk_coremap);
        paddr_t pa = KVADDR_TO_PADDR(addr);
        unsigned index = pa/PAGE_SIZE;
        unsigned block_size = coremap[index].block_size;
        num_allocated_pages -= block_size;
        for(unsigned i = index; i < index + block_size; i++) {
            coremap[i].page_state = PS_FREE;
            coremap[i].block_size = 0;
	    coremap[i].vaddr = 0;
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
    splx(spl);
    V(sem_tlb);
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

            break;
        case VM_FAULT_READ:

            break;
        case VM_FAULT_WRITE:

            break;
        default:
            return EINVAL;
    }

    //check if page_fault
    struct page_table_entry *pte = get_pte(as,faultaddress);
    if(pte == NULL) {
        pte = add_pte(as,faultaddress,0);
        if(pte == NULL) {
		lock_release();
        	return ENOMEM;
        }
    }
    lock_acquire(pte->pte_lock);
    if(pte->paddr == 0 || pte->on_disk) {
	pte->paddr = page_alloc(1,faultaddress,as);
        if(pte->paddr == 0) {
		lock_release(pte->pte_lock);
            	return ENOMEM;
        }
	if(pte->on_disk) {
		int err = swap_in(pte);
		if(err) {
			lock_release(pte->pte_lock);
			return err;
		}
	}
        coremap[pte->paddr/PAGE_SIZE].page_state = PS_DIRTY;
	coremap[pte->paddr/PAGE_SIZE].cpu = curcpu->c_number;
    }

    int spl = splhigh();
    uint32_t ehi = faultaddress;
    uint32_t elo = pte->paddr | TLBLO_VALID;

    if((permission & AS_WRITEABLE) == AS_WRITEABLE) {
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
    splx(spl);
    lock_release(pte->pte_lock);

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
    if(vaddr >= USERSTACKTOP && vaddr < USERSTACK) {
        *permission = AS_READABLE | AS_WRITEABLE;
        return true;
    }

    *permission = 0;
    return false;
}

void
debug_vm(void)
{
    kprintf("\nleaking pages: ");
    for(unsigned i=num_fixed+1;i<num_total_pages;i++) {
        if(coremap[i].page_state != PS_FREE) {
            kprintf("%u ",i);
        }
    }
    kprintf("\nfreed Fixed pages: ");
    for(unsigned i = 0; i <= num_fixed; i++) {
        if(coremap[i].page_state == PS_FREE) {
            kprintf("%u ",i);
        }
    }
}

int
evict_page(int index)
{
	struct pagte_table_entry *evict_pte = get_pte(coremap[index].vaddr,coremap[index].as);
	if(evict_pte == NULL) {
		coremap[index].busy = false;
		return 0;
	}
	lock_acquire(evict_pte->pte_lock);
	if(evict_pte->on_disk) {
		coremap[index].busy = false;
		lock_release(evict_pte->pte_lock);
		return 0;
	}
	tlbshootdown(evict_pte,coremap[index].cpu);
	int err = swap_out(evict_pte);
	if(err) {
		lock_release(evict_pte->pte_lock);
		return err;
	}
	lock_release(evict_pte->pte_lock);
	return 0;
}

int
swap_out(struct page_table_entry *pte)
{
	lock_acquire(swap_lock);
	for(int i = 0; i < MAX_SWAP; i++) {
		if(swapmap[i] == false) {
			pte->swap_index = i;
			swapmap[i] = true;
			break;
		}
	}
	lock_release(swap_lock);
	
	struct iovec;
	struct kuio;
	uio_kinit(&iovec, &kuio, pte->paddr, PAGE_SIZE, pte->swap_index*PAGE_SIZE, UIO_WRITE);
	int err = VOP_WRITE(swap_disk,&kuio);
	if(err) {
		return err;
	}
	pte->paddr = 0;
	pte->on_disk = true;
}

int
swap_in(struct pagte_table_entry *pte)
{
	struct iovec;
	struct kuio;
	uio_kinit(&iovec, &kuio, pte->paddr, PAGE_SIZE, pte->swap_index*PAGE_SIZE, UIO_READ);
	int err = VOP_WRITE(swap_disk,&kuio);
	if(err) {
		return err;
	}
	pte->on_disk = false;

}
