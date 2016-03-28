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

vaddr_t
alloc_kpages(unsigned npages)
{
    paddr_t pa;
    if(vm_bootstrapped) {
        int start_index = -1;
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
            if(coremap[i].page_state == PS_FREE
                    || coremap[i].page_state == PS_CLEAN
                    || coremap[i].page_state == PS_DIRTY) {
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
            coremap[j].page_state = PS_FIXED;
            coremap[j].block_size = npages;
            coremap[j].vaddr = PADDR_TO_KVADDR(pa);
            coremap[j].as = NULL;
        }
        bzero((void *)coremap[start_index].vaddr, npages*PAGE_SIZE);
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
    (void)ts;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void)faulttype;
    (void)faultaddress;
    return 0;
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
