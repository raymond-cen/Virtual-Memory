/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _VM_H_
#define _VM_H_

#include <addrspace.h>
/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */
#define USER_STACK_SIZE 16 * PAGE_SIZE

struct addrspace *as;
// Helper Function declarations.
paddr_t get_first_level_bits(vaddr_t vaddr);
paddr_t get_second_level_bits(vaddr_t vaddr);
paddr_t get_third_level_bits(vaddr_t vaddr);

struct region *get_region(struct addrspace *as, vaddr_t vaddr);



// Insert, lookup, update page table function declarations.
int insert_pte(struct addrspace *as, vaddr_t vaddr, paddr_t paddr);
paddr_t lookup_pte(struct addrspace *as, vaddr_t v_ddr);
int update_pte(struct addrspace *as, vaddr_t vaddr, paddr_t paddr);


#include <machine/vm.h>

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/


/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

void vm_freePTE(paddr_t ***pte);
vaddr_t alloc_frame(void);
int copyPTE(struct addrspace *old, struct addrspace *newas);
int vm_initPT(paddr_t ***oldPTE, vaddr_t vaddr);
int vm_addPTE(paddr_t ***oldPTE, vaddr_t faultaddress, uint32_t dirty);
int lookup_region(struct addrspace *as, vaddr_t vaddr, int faulttype);
int probe_pt(struct addrspace *as, vaddr_t vaddr);
#endif /* _VM_H_ */
