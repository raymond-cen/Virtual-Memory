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
    paddr_t pbase = KVADDR_TO_PADDR(vaddr);
    return pbase >> 24;
}

vaddr_t get_second_level_bits(vaddr_t vaddr) {
    paddr_t pbase = KVADDR_TO_PADDR(vaddr);
    pbase = pbase << 8;
    return pbase >> 26;
}   

vaddr_t get_third_level_bits(vaddr_t vaddr) {
    paddr_t pbase = KVADDR_TO_PADDR(vaddr);
    pbase = pbase << 14;
    return pbase >> 26;
}

// Gets region 
// If the address is within the region, return that region.
// Returns NULL otherwise.
struct region *get_region(struct addrspace *as, vaddr_t vaddr) {
    struct region *curr = as->as_regions;
    while (curr != NULL) {
        if (vaddr >= curr->vbase && (vaddr < (curr->vbase + curr->sz))) {
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
        as->pagetable[p1_bits] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE);

        // Returns error if kmalloc of pagetable lvl 2 fails.
        if (as->pagetable[p1_bits] == NULL) {
            return ENOMEM;
        }
         
        // Set non-leaf nodes to NULL.
        for (int i = 0; i < 64; i++) {
            as->pagetable[p1_bits][i] = NULL;
        }
    }
    if (as->pagetable[p1_bits][p2_bits] == NULL) {
        as->pagetable[p1_bits][p2_bits] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE2);

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
    if (p1_bits >= PAGETABLE_SIZE || p2_bits >= PAGETABLE_SIZE2 || p3_bits >= PAGETABLE_SIZE2) {
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
        return -1;
    }
    return 0;

}

/* Updates page table entry with physical address
 * Returns 0 upon sucess.
 */
int update_pte(struct addrspace *as, vaddr_t vaddr, paddr_t paddr) {
    paddr_t ret = lookup_pte(as, vaddr);
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

    int res = lookup_region(as, faultaddress, faulttype); 
    if(res) {
        // kfree(as_pagetable[p1_bits]);
        // as_pagetable[p1_bits] = NULL;
        // panic("%d", faultaddress);
        return res;
    }

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
    if (as_pagetable[p1_bits][p2_bits] == NULL) {
        as_pagetable[p1_bits][p2_bits] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE2);
        if (as_pagetable[p1_bits][p2_bits] == NULL) {
            kfree(as_pagetable[p1_bits][p2_bits]);
            as_pagetable[p1_bits][p2_bits] = NULL;
            return ENOMEM;
        }
        // memset(oldPTE[p1_bits][p2_bits], 0, sizeof(paddr_t)*PAGETABLE_SIZE2);
        for (int i = 0; i < PAGETABLE_SIZE2; i++) {
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

    // int res = lookup_region(as, faultaddress, faulttype); 
    // if(res) {
    //     kfree(as_pagetable[p1_bits]);
    //     as_pagetable[p1_bits] = NULL;
    //     return res;
    // }
    // struct region *curr = as->as_regions;

    // while(curr != NULL) {
    //     if (faultaddress >= curr->vbase && (faultaddress - curr->vbase) < curr->sz) {
    //         break;
    //     }
    //     curr = curr->next;
    // }
    // if (curr == NULL) {
    //     int stack_size = 16 * PAGE_SIZE;
    //     if (faultaddress < USERSTACK && faultaddress > (USERSTACK - stack_size)) {
            
    //     } else {
    //         return EFAULT;
    //     }
    // }


    // int result = 0;
    // uint32_t dirty = 0;
    // int flag = 0;
    //  if (as->pagetable[p1_bits] == NULL) {
    //     result = vm_initPT(as->pagetable, faultaddress);
    //      if (result) {
    //          return result;
    //      }
    //      flag = 1;
    //  }

    //  if (as->pagetable[p1_bits][p2_bits][p3_bits] == 0) {
    //      struct region *oregion = as->as_regions;
    //      while (oregion != NULL) {
    //         if (faultaddress >= (oregion -> vbase + oregion -> sz) 
    //             && faultaddress < oregion -> vbase) continue;
    //         // READONLY
    //         if (oregion -> writeable != 1) dirty = 0;
    //         else dirty = TLBLO_DIRTY;
    //         break;
    //         oregion = oregion->next;
    //      }
    //      if (oregion == NULL) {
    //          if (flag) {
    //              kfree(as->pagetable[p1_bits][p2_bits]);
    //              kfree(as->pagetable[p1_bits]);
    //              as->pagetable[p1_bits][p2_bits] = NULL;
    //              as->pagetable[p1_bits] = NULL;
    //          } 
    //          return EFAULT;
    //      }

    //      result = vm_addPTE(as->pagetable, faultaddress, dirty);
    //      if (result) {
    //          if (flag) {
                 
    //             kfree(as->pagetable[p1_bits][p2_bits]);
    //             kfree(as->pagetable[p1_bits]);
    //             as->pagetable[p1_bits][p2_bits] = NULL;
    //             as->pagetable[p1_bits] = NULL;
    //          } 
    //          return result;
    //      }
    //  }
    // vaddr_t newVaddr = alloc_kpages(1);
    // if (newVaddr == 0) {
    //     return ENOMEM;
    // }
    // bzero((void *) newVaddr, PAGE_SIZE); 
    // paddr_t paddr = KVADDR_TO_PADDR(newVaddr) & PAGE_FRAME;
    // struct region *oRegions = get_region(as, faultaddress);
    // if (oRegions == NULL) return EFAULT;
    // if (oRegions->writeable != 0) {
    //     paddr = paddr | TLBLO_DIRTY;
    // }
    // int result = insert_pte(as, faultaddress, paddr | TLBLO_VALID);
    // if (result != 0) {
    //     return result;
    // }
    // // // Load tlb.
    int sql = splhigh();
    tlb_random(faultaddress & PAGE_FRAME, as_pagetable[p1_bits][p2_bits][p3_bits]);
    splx(sql);
    // panic("%#08x", faultaddress);
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
    for (int i = 0; i < 256; i++) {
        if (pte[i] == NULL) continue;

        for (int j = 0; j < 64; j ++) {
			if (pte[i][j] == NULL) continue;
			for (int k = 0; k < 64; k++) {
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
	//bzero((void *) newVaddr, regionSize);
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
			return ENOMEM;
		}

		for (int j = 0; j < PAGETABLE_SIZE2; j++) {
			if (old->pagetable[i][j] == NULL) {
				continue;
			} 
			newas->pagetable[i][j] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE2);
			if (newas->pagetable[i][j] == NULL) {
				return ENOMEM;
			}
			for (int k = 0; k < PAGETABLE_SIZE2; k++) {
				if (old->pagetable[i][j][k] == 0) {
					newas->pagetable[i][j][k] = 0;
				} else {
					vaddr_t newframe = alloc_kpages(1);
					if (newframe == 0) {
						return ENOMEM; // Out of memory
					}
                    memmove((void *)newframe, (const void *)PADDR_TO_KVADDR(old->pagetable[i][j][k] & PAGE_FRAME), PAGE_SIZE);
                    newas->pagetable[i][j][k] = (KVADDR_TO_PADDR(newframe) & PAGE_FRAME) | (old->pagetable[i][j][k] & TLBLO_DIRTY) | TLBLO_VALID;
					// bzero((void *)newframe, PAGE_SIZE);
					// // copy bytes
					// if (memmove((void *)newframe, (const void *)PADDR_TO_KVADDR(old->pagetable[i][j][k] & PAGE_FRAME)
					// 	, PAGE_SIZE) == NULL) { // fail memmove()
					// 	vm_freePTE(newas->pagetable);
					// 	return ENOMEM; // Out of memory
					// }
					// newas->pagetable[i][j][k] = (KVADDR_TO_PADDR(newframe) & PAGE_FRAME) | 
					// (TLBLO_DIRTY & old->pagetable[i][j][k]) | (TLBLO_VALID & old->pagetable[i][j][k]);
				}
			}
		}
		
	}
    return 0;
}


int vm_initPT(paddr_t ***oldPTE, vaddr_t vaddr)
{
    uint32_t p1_bits = get_first_level_bits(vaddr);
    // uint32_t p2_bits = get_second_level_bits(vaddr);
    
    oldPTE[p1_bits] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE);
    if (oldPTE[p1_bits] == NULL) return ENOMEM;

    for (int i = 0; i < PAGETABLE_SIZE; i++) {
        oldPTE[p1_bits][i] = NULL;
    }
    // oldPTE[p1_bits][p2_bits] = kmalloc(sizeof(paddr_t) * PAGETABLE_SIZE2);
    // if (oldPTE[p1_bits][p2_bits] == NULL) {
    //     return ENOMEM;
    // }
    // // memset(oldPTE[p1_bits][p2_bits], 0, sizeof(paddr_t)*PAGETABLE_SIZE2);
    // for (int i = 0; i < PAGETABLE_SIZE2; i++) {
    //     oldPTE[p1_bits][p2_bits][i] = 0;
    // }
    return 0;
}

int vm_addPTE(paddr_t ***oldPTE, vaddr_t faultaddress, uint32_t dirty)
{
    uint32_t p1_bits = get_first_level_bits(faultaddress);
    uint32_t p2_bits = get_second_level_bits(faultaddress);
    uint32_t p3_bits = get_third_level_bits(faultaddress);
    vaddr_t vbase = alloc_kpages(1);
    if (vbase == 0) return ENOMEM;
    paddr_t pbase = KVADDR_TO_PADDR(vbase);
    bzero((void *)PADDR_TO_KVADDR(pbase), PAGE_SIZE);
    

    oldPTE[p1_bits][p2_bits][p3_bits] = (pbase & PAGE_FRAME) | dirty | TLBLO_VALID;
    // panic("vm: vm_addPTE DONE\n");
    return 0;
}

int probe_pt(struct addrspace *as, vaddr_t vaddr) {
    uint32_t p1_bits = get_first_level_bits(vaddr);
    uint32_t p2_bits = get_second_level_bits(vaddr);
    uint32_t p3_bits = get_third_level_bits(vaddr);

    if (p1_bits >= PAGETABLE_SIZE || p2_bits >= PAGETABLE_SIZE2 || p3_bits >= PAGETABLE_SIZE2) {
        return ERANGE; //TODO: REPLACE with OUT OF RANGE
    }
    if (as->pagetable == NULL) {
        return -1;
    }
    if (as->pagetable[p1_bits] == NULL) {
        return -1;
    }
    if (as->pagetable[p1_bits][p2_bits] == NULL) {
        return -1;
    }

    if (as->pagetable[p1_bits][p2_bits][p3_bits] == 0) {
        return -1;
    } 
    return 0;
}

int lookup_region(struct addrspace *as, vaddr_t vaddr, int faulttype) {
    struct region *curr = as->as_regions;
    while(curr != NULL) {
        if (vaddr >= curr->vbase &&
            (vaddr < (curr->vbase + curr->sz))) {
            break;
        }
        curr = curr->next;
    }

    if (curr == NULL)     return EFAULT; /* Cant find region thus return error */

    switch (faulttype)
    {
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

