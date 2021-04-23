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

vaddr_t get_first_level_bits(vaddr_t vaddr) {
    return vaddr >> 24;
}

vaddr_t get_second_level_bits(vaddr_t vaddr) {
    vaddr << 8;
    return vaddr >> 26;
}   

vaddr_t get_third_level_bits(vaddr_t vaddr) {
    vaddr << 14;
    return vaddr >> 26;
}

// Gets region 
// If the address is within the region, return that region.
// Returns NULL otherwise.
struct region *get_region(struct addrspace *as, vaddr_t vaddr) {
    struct region *curr = as->as_regions;

    while (curr != NULL) {
        if (vaddr >= curr->vbase && vaddr < curr->vbase + curr->sz) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
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
    // Returns EFAULT if faulttype is VM_FAULT_READONLY.
    switch(faulttype) {
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        case VM_FAULT_READONLY:
            return EFAULT;
        default:
            return EINVAL;
    }

    if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

    if (faultaddress == 0) {
        return EFAULT;
    }

    struct addrspace *as = proc_getas()
    if (as == NULL) {
        return EFAULT;
    }

    paddr_t ***as_pagetable = as->pagetable;

    // Get bits.
    uint32_t p1_bits = get_first_level_bits(faultaddress);
    uint32_t p2_bits = get_second_level_bits(faultaddress);
    uint32_t p3_bits = get_third_level_bits(faultaddress);

    // Returns error if pagetable index is out of bounds.
    if (p1_bits >= 256 || p2_bits >= 64 || p3_bits >= 64) {
        return ERANGE;
    }

    // Gets region.
    struct region *region = get_region(as, faultaddress);
    if (region == NULL) {
        return EFAULT;
    }
    // Checks for correct bits.
    switch (faulttype) {
        case VM_FAULT_READ:
            if (region->readable == 0) {
                return EPERM;
            }
        case VM_FAULT_WRITE:
            if (region->writeable == 0) {
                return EPERM;
            }
        default:
            return EINVAL;
    }

    // Lookup PT and load tlb if translation found.
    if (lookup_pte(as, faultaddress) != 0) {
        int sql = splhigh();
        tlb_random(faultaddress & PAGE_FRAME, as_pagetable[p1_bits][p2_bits][p3_bits]);
        splx(sql);
        return 0;
    }

    // Allocate frame, zero-fill and insert pte.
    vaddr_t vaddr = alloc_kpages(1);
    if (vaddr == 0) {
        return ENOMEM;
    }
    bzero((void *)vaddr, PAGE_SIZE);
    paddr_t paddr = KVADDR_TO_PADDR(vaddr) & PAGE_FRAME;

    if (region->writeable != 0) {
        paddr = paddr | TLBLO_DIRTY;
    }
    
    paddr = paddr | TLBLO_VALID;

    int ret = insert_pte(as, faultaddress, paddr);
    if (ret != 0) {
        return ret;
    }

    // Load tlb.
    int sql = splhigh();
    tlb_random(faultaddress & PAGE_FRAME, as_pagetable[p1_bits][p2_bits][p3_bits]);
    splx(sql);
    return 0;
    
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

