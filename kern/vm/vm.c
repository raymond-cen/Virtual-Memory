#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <proc.h>
#include <current.h>
#include <spl.h>

/* Place your page table functions here */

// Helper Functions

vaddr_t get_first_level_bits(vaddr_t addr) {
    return addr >> 24;
}

vaddr_t get_second_level_bits(vaddr_t addr) {
    addr << 8;
    return addr >> 26;
}   

vaddr_t get_third_level_bits(vaddr_t addr) {
    addr << 14;
    return addr >> 26;
}


void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

/* Inserts page table entry.
 * Returns 0 upon success.
 */
int insert_pte(struct addrspace *as, vaddr_t vaddr, paddr_t paddr) {
    uint32_t p1_bits = get_first_level_bits(vaddr);
    uint32_t p2_bits = get_second_level_bits(vaddr);
    uint32_t p3_bits = get_third_level_bits(vaddr);

    // Returns error if pagetable index is out of bounds.
    if (p1_bits >= 256 || p2_bits >= 64 || p3_bits >= 64) {
        return ERANGE;
    }

    // Returns error if pagetable isn't created.
    if (as->pagetable == NULL) {
        return EINVAL;
    }

    if (as->pagetable[p1_bits] == NULL) {
        as->pagetable[p1_bits] = kmalloc(sizeof(paddr_t) * 64);

        // Returns error if kmalloc of pagetable lvl 2 fails.
        if (as->pagetable[p1_bits] == NULL) {
            return ENOMEM;
        }

        // Set non-leaf nodes to NULL.
        for (int i = 0; i < 64, i++) {
            as->pagetable[p1_bits][i] = NULL;
        }
    }
    if (as->pagetable[p1_bits][p2_bits] == NULL) {
        as->pagetable[p1_bits][p2_bits] = kmalloc(sizeof(paddr_t) * 64);

        // Returns error if kmalloc of pagetable lvl 3 fails.
        if (as->pagetable[p1_bits][p2_bits] == NULL) {
            return ENOMEM;
        }

        // Set leaf nodes to 0.
        for (int i = 0; i < 64; i++) {
            as->pagetable[p1_bits][p2_bits][i] = 0;
        }
    }
    as->pagetable[p1_bits][p2_bits][p3_bits] = paddr;

    return 0;
}

/* Looks up physical address im page table.
 * Returns EINVAL if page table levels not linked.
 * Returns 0 if physical address is not in pte.
 */
paddr_t lookup_pte(struct addrspace *as, vaddr_t vaddr) {
    uint32_t p1_bits = get_first_level_bits(vaddr);
    uint32_t p2_bits = get_second_level_bits(vaddr);
    uint32_t p3_bits = get_third_level_bits(vaddr);

    // Returns error if pagetable index is out of bounds.
    if (p1_bits >= 256 || p2_bits >= 64 || p3_bits >= 64) {
        return ERANGE;
    }

    if (as->pagetable == NULL) {
        return EINVAL;
    }
    if (as->pagetable[p1_bits] == NULL) {
        return EINVAL;
    }
    if (as->pagetable[p1_bits][p2_bits] == NULL) {
        return EINVAL;
    }
    if (as->pagetable[p1_bits][p2_bits][p3_bits] == 0) {
        return 0;
    }
    return as->pagetable[p1_bits][p2_bits][p3_bits];

}

/* Updates page table entry with physical address
 * Returns 0 upon sucess.
 */
int update_pte(struct addrspace *as, vaddr_t vaddr, paddr_t paddr) {
    paddr_t ret = lookup_pte(as, vaddr, paddr);
    if (ret == EINVAL) {
        return EINVAL;
    }
    
    uint32_t p1_bits = get_first_level_bits(vaddr);
    uint32_t p2_bits = get_second_level_bits(vaddr);
    uint32_t p3_bits = get_third_level_bits(vaddr);

    as->pagetable[p1_bits][p2_bits][p3_bits] = paddr;
    return 0;
}


int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void) faulttype;
    (void) faultaddress;

    panic("vm_fault hasn't been written yet\n");

    return EFAULT;
}







/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

