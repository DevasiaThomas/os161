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
struct spinlock splk_tlb;
struct spinlock splk_copy;
unsigned num_allocated_pages = 0;
unsigned num_total_pages = 0;
struct coremap_entry *coremap;
unsigned num_fixed = 0;
bool vm_bootstrapped = false;
static unsigned search_start = 0;

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

    spinlock_init(&splk_tlb);
    spinlock_init(&splk_copy);
}

paddr_t
page_alloc(unsigned npages, vaddr_t vaddr,struct addrspace *as)
{
    int start_index = -1;
    paddr_t pa;
    spinlock_acquire(&splk_coremap);
    if(num_allocated_pages + npages >= num_total_pages - num_fixed) {
        spinlock_release(&splk_coremap);
        return 0;
    }
    unsigned i = search_start;
    while(true) {
        if(i+npages-1 >= num_total_pages) {
            i = num_fixed + 1;
            continue;
        }
        if(coremap[i].page_state == PS_FREE) { //|| coremap[i].page_state == PS_CLEAN || coremap[i].page_state == PS_DIRTY) {
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
        coremap[j].page_state = (as == NULL)?PS_FIXED:PS_VICTIM;
        coremap[j].block_size = npages;
        coremap[j].vaddr = (as==NULL)?PADDR_TO_KVADDR(pa):(vaddr & PAGE_FRAME);
        coremap[j].as = as;
    }

    bzero((void *)PADDR_TO_KVADDR(pa), npages*PAGE_SIZE);

    return pa;
}

void
page_free(paddr_t paddr)
{
    spinlock_acquire(&splk_coremap);
    unsigned index = paddr/PAGE_SIZE;
    coremap[index].page_state = PS_FREE;
    coremap[index].block_size = 0;
    coremap[index].vaddr = 0;
    coremap[index].as = NULL;
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
    if(curproc->p_addrspace == ts->ts_as) {
        int index = tlb_probe(ts->ts_vaddr,0);
        if(index > 0) {
            tlb_write(TLBHI_INVALID(index),TLBLO_INVALID(),index);
        }
    }
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
        return ENOMEM;
        }
    }
    if(pte->paddr == 0) {
        vaddr_t vaddr_temp = faultaddress & PAGE_FRAME;
        pte->paddr = page_alloc(1,vaddr_temp,as);
        if(pte->paddr == 0) {
            return ENOMEM;
        }
        coremap[pte->paddr/PAGE_SIZE].page_state = PS_DIRTY;
    }

    spinlock_acquire(&splk_tlb);
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
    spinlock_release(&splk_tlb);


    return 0;
}

bool
check_if_valid(vaddr_t vaddr, struct addrspace *as,int *permission)
{
    struct region_entry *t_reg = as->regions;
    while(t_reg) {
        if(vaddr >= t_reg->reg_base && vaddr <= t_reg->reg_base + t_reg->bounds) {
            *permission = t_reg->original_permissions;
            return true;
        }
        t_reg = t_reg->next;
    }
    if(vaddr >= as->heap_start && vaddr <= as->heap_end) {
        *permission = AS_READABLE | AS_WRITEABLE;
        return true;
    }
    if(vaddr >= USERSTACKBASE && vaddr <= USERSTACK) {
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
