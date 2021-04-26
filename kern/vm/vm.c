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

paddr_t get_first_level_bits(vaddr_t vaddr) {
    paddr_t pbase = KVADDR_TO_PADDR(vaddr);
    return pbase >> 24;
}

paddr_t get_second_level_bits(vaddr_t vaddr) {
    paddr_t pbase = KVADDR_TO_PADDR(vaddr);
    pbase = pbase << 8;
    return pbase >> 26;
}   

paddr_t get_third_level_bits(vaddr_t vaddr) {
    paddr_t pbase = KVADDR_TO_PADDR(vaddr);
    pbase = pbase << 14;
    return pbase >> 26;
}

void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
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
        panic("no curproc\n");
		return EFAULT;
	}

    struct addrspace *as = proc_getas();
    if (as == NULL) {
        return EFAULT;
    }

    paddr_t ***as_pagetable = as->pagetable;
    bool new = false;
    int result = 0;
    uint32_t dirty = 0;
    // Get bits.
    uint32_t p1_bits = get_first_level_bits(faultaddress);
    uint32_t p2_bits = get_second_level_bits(faultaddress);
    uint32_t p3_bits = get_third_level_bits(faultaddress);

    // Returns error if pagetable index is out of bounds.
    if (p1_bits >= 256 || p2_bits >= 64 || p3_bits >= 64) {
        return ERANGE;
    }

    // Check for valid region
    int res = lookup_region(as, faultaddress, faulttype); 
    if(res) {
        return res;
    }

    // Look up Page Table
    if (as_pagetable[p1_bits] == NULL) {
        int res = lookup_region(as, faultaddress, faulttype); 
        if(res) {
            kfree(as_pagetable[p1_bits]);
            as_pagetable[p1_bits] = NULL;
            return res;
        }
        result = vm_initPT(as->pagetable, faultaddress);
        if (result) return result;
        new = true;
    }
    // Allocate frame
    if (as_pagetable[p1_bits][p2_bits] == NULL) {
        as_pagetable[p1_bits][p2_bits] = (paddr_t*)kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE_2);
        if (as_pagetable[p1_bits][p2_bits] == NULL) {
            kfree(as_pagetable[p1_bits][p2_bits]);
            as_pagetable[p1_bits][p2_bits] = NULL;
            return ENOMEM;
        }

        for (int i = 0; i < PAGETABLE_SIZE_3; i++) {
            as_pagetable[p1_bits][p2_bits][i] = 0;
        }
    }
    if (as_pagetable[p1_bits][p2_bits][p3_bits] == 0) {
        struct region *curr = as->as_regions;

        while(curr != NULL) {
            if (faultaddress >= curr->vbase && faultaddress < (curr->vbase + (curr->sz))) {
                if (curr->writeable) {
                    dirty = TLBLO_DIRTY;
                } else {
                    dirty = 0;
                }
                break;
            }
            curr = curr->next;
        }
        if (curr == NULL) {
            if (new && as_pagetable[p1_bits] != NULL) {
                kfree(as_pagetable[p1_bits]);
                as_pagetable[p1_bits] = NULL;
            }
            return EFAULT;
        }
        if (curr->writeable) {
            dirty = TLBLO_DIRTY;
        } else {
            dirty = 0;
        }
        result = vm_addPTE(as->pagetable, faultaddress, dirty);
        if (result) {
            if (new && as->pagetable[p1_bits]) {
                kfree(as->pagetable[p1_bits]);
                as->pagetable[p1_bits] = NULL;
            }
            return result;
        }
    }
    // Save into tlb
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


void vm_freePTE(paddr_t ***pte)
{
    // page table is associated with KUSEG which will not have an entry 'higher' than pt[127][63][63]
    for (int i = 0; i < (PAGETABLE_SIZE / 2); i++) { 
        if (pte[i] == NULL) continue;

        for (int j = 0; j < PAGETABLE_SIZE_2; j ++) {
			if (pte[i][j] == NULL) continue;
			for (int k = 0; k < PAGETABLE_SIZE_3; k++) {
				if (pte[i][j][k] != 0) {
					free_kpages(PADDR_TO_KVADDR(pte[i][j][k] & PAGE_FRAME));
				} 
			}
            kfree(pte[i][j]);
            pte[i][j] = NULL;
        }
        kfree(pte[i]);
        pte[i] = NULL;
    }
    kfree(pte); // Free page table entry
}

vaddr_t alloc_frame() {
	/*  Allocate Frame for this region  */
    vaddr_t newVaddr = alloc_kpages(1);
	if (newVaddr == 0) return 0;
	/* zero out the frame */
    bzero((void *) newVaddr, PAGE_SIZE);
    return newVaddr;
}

int copyPTE(struct addrspace *old, struct addrspace *newas) {
	for (int i = 0; i < PAGETABLE_SIZE; i++) {
		if (old->pagetable[i] == NULL) {
			continue;
		}
		newas->pagetable[i] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE);
		if (newas->pagetable[i] == NULL) {
			return ENOMEM; // Out of memory
		}

		for (int j = 0; j < PAGETABLE_SIZE_2; j++) {
			if (old->pagetable[i][j] == NULL) {
				continue;
			} 
			newas->pagetable[i][j] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE_2);
			if (newas->pagetable[i][j] == NULL) {
				return ENOMEM; // Out of memory
			}
			for (int k = 0; k < PAGETABLE_SIZE_3; k++) {
				if (old->pagetable[i][j][k]) { // Check if it is empty
					vaddr_t newframe = alloc_frame();
					if (newframe == 0) {
						return ENOMEM; // Out of memory
					}
                    if (memmove((void *)newframe, (const void *)PADDR_TO_KVADDR(old->pagetable[i][j][k] & PAGE_FRAME)
                    , PAGE_SIZE) == NULL) { // fail memove
                        vm_freePTE(newas->pagetable);
                        return ENOMEM;
                    }
                    newas->pagetable[i][j][k] = (KVADDR_TO_PADDR(newframe) & PAGE_FRAME) | (old->pagetable[i][j][k] & TLBLO_DIRTY) | TLBLO_VALID;
				} else {
                    newas->pagetable[i][j][k] = 0;
				}
			}
		}
		
	}
    return 0;
}

// Allocate 1st level entry
int vm_initPT(paddr_t ***oldPTE, vaddr_t vaddr) {
    uint32_t p1_bits = get_first_level_bits(vaddr);
    
    oldPTE[p1_bits] = (paddr_t**)kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE_2);
    if (oldPTE[p1_bits] == NULL) return ENOMEM;

    for (int i = 0; i < PAGETABLE_SIZE_2; i++) {
        oldPTE[p1_bits][i] = NULL;
    }
    return 0;
}

// Allocate the 3rd level entry 
int vm_addPTE(paddr_t ***oldPTE, vaddr_t faultaddress, uint32_t dirty) {
    uint32_t p1_bits = get_first_level_bits(faultaddress);
    uint32_t p2_bits = get_second_level_bits(faultaddress);
    uint32_t p3_bits = get_third_level_bits(faultaddress);
    vaddr_t vbase = alloc_kpages(1);
    if (vbase == 0) return ENOMEM;
    paddr_t pbase = KVADDR_TO_PADDR(vbase);
    bzero((void *)PADDR_TO_KVADDR(pbase), PAGE_SIZE);
    
    oldPTE[p1_bits][p2_bits][p3_bits] = (pbase & PAGE_FRAME) | dirty | TLBLO_VALID;
    return 0;
}

// finds the region where the faultaddress is located and checks if it is valid
int lookup_region(struct addrspace *as, vaddr_t vaddr, int faulttype) {
    struct region *curr = as->as_regions;
    while(curr != NULL) {
        if (vaddr >= curr->vbase && (vaddr < (curr->vbase + curr->sz))) {
            break;
        }
        curr = curr->next;
    }

    if (curr == NULL) return EFAULT; /* Cant find region thus return error */

    switch (faulttype) {
        case VM_FAULT_WRITE:
            if (curr->writeable == 0) return EPERM;
            break;
        case VM_FAULT_READ:
            if (curr->readable == 0) return EPERM;
            break;
        default:
            return EINVAL; /* Invalid Arg */
    }

    return 0;
}
