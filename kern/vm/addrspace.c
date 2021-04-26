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

 #include <types.h>
 #include <kern/errno.h>
 #include <lib.h>
 #include <spl.h>
 #include <spinlock.h>
 #include <current.h>
 #include <mips/tlb.h>
 #include <addrspace.h>
 #include <vm.h>
 #include <proc.h>
 
 /*
  * Note! If OPT_DUMBVM is set, as is the case until you start the VM
  * assignment, this file is not compiled or linked or in any way
  * used. The cheesy hack versions in dumbvm.c are used instead.
  *
  * UNSW: If you use ASST3 config as required, then this file forms
  * part of the VM subsystem.
  *
  */
 
 struct addrspace *
 as_create(void)
 {
     struct addrspace *as;
     as = kmalloc(sizeof(struct addrspace));
     if (as == NULL) {
         return NULL;
     }
     /*
      * Initialize as needed.
      */
     as->as_regions = NULL; /* region initialisation */
     /* PD initialisation */
     paddr_t ***pd = (paddr_t ***)alloc_kpages(1);
     if(pd == NULL) {
         kfree(as);
         return NULL;
     }
     
     for (int i = 0; i < PAGETABLE_SIZE; i++) {
         pd[i] = NULL;
     }
     as->pagetable = pd;
     return as;
 }
 
 int
 as_copy(struct addrspace *old, struct addrspace **ret)
 {
     struct addrspace *newas;
 
     newas = as_create();
     if (newas==NULL) {
         panic("newas allocate");
         return ENOMEM;
     }
 
     /*
      * Write this.
      */
     if (old == NULL) {
         return EINVAL;
     }
     struct region *old_region = old->as_regions;
     struct region *new_region = newas->as_regions;
 
     while (old_region != NULL) {
         struct region *temp = kmalloc(sizeof(struct region));
         if (temp == NULL) {
             as_destroy(newas);
             panic("tempregion allocate");
             return ENOMEM;
         }
         // copy regions
         temp->vbase = old_region->vbase;
         temp->sz = old_region->sz;
         temp->readable = old_region->readable;
         temp->writeable = old_region->writeable;
         temp->writeable_prev = old_region->writeable_prev;
         temp->executable = old_region->executable;
         temp->next = NULL;
 
         if (new_region != NULL) {
             new_region->next = temp;
         } else {
             newas->as_regions = temp;
         }
         new_region = temp;
         old_region = old_region->next;
     }
     int result = copyPTE(old, newas);
     if (result) {
         as_destroy(newas);
         return result;
     }
 
     *ret = newas;
     return 0;
 }
 
 void
 as_destroy(struct addrspace *as)
 {
     if (as == NULL) {
         return;
     }
     /*
      * Clean up as needed.
      */
     struct region *temp = NULL;
     struct region *head = as->as_regions;
     while (head != NULL) {
         temp = head;
         head = head->next;
         kfree(temp);
     }
    
    //  if (temp != NULL) {
    //     kfree(temp);
    //     temp = NULL;
    //  }
    as->as_regions = NULL; 
    vm_freePTE(as -> pagetable);
    as->pagetable = NULL;
    kfree(as);
    as = NULL;
 }
 
 void
 as_activate(void)
 {
     int i, spl;
     struct addrspace *as;
 
     as = proc_getas();
     if (as == NULL) {
         /*
          * Kernel thread without an address space; leave the
          * prior address space in place.
          */
         return;
     }
 
     /*
      * Write this. Copied from dumbvm.c
      */
     /* Disable interrupts on this CPU while frobbing the TLB. */
     spl = splhigh();
 
     for (i=0; i<NUM_TLB; i++) {
         tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
     }
 
     splx(spl);
 }
 
 void
 as_deactivate(void)
 {
     /*
      * Write this. For many designs it won't need to actually do
      * anything. See proc.c for an explanation of why it (might)
      * be needed.
      */
     as_activate();
 }
 
 /*
  * Set up a segment at virtual address VADDR of size MEMSIZE. The
  * segment in memory extends from VADDR up to (but not including)
  * VADDR+MEMSIZE.
  *
  * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
  * write, or execute permission should be set on the segment. At the
  * moment, these are ignored. When you write the VM system, you may
  * want to implement them.
  */
 int
 as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
          int readable, int writeable, int executable)
 {
     /*
      * Write this.
      */
     if (as == NULL) {
         return EFAULT;
     }
     // if (vaddr + memsize >= USERSTACK) {
     // 	panic("stack allocate");
     // 	return ENOMEM;
     // } 
     /* Align the region. First, the base... */
     memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
     vaddr &= PAGE_FRAME;
 
     /* ...and now the length. */
     memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
     if ((vaddr + memsize) > MIPS_KSEG0) {
         return EFAULT;
     }
     struct region *new = kmalloc(sizeof(struct region));
     if (new == NULL) {
         panic("region allocate");
         return ENOMEM;
     }
     new->vbase = vaddr;
     new->sz = memsize;
     new->readable = readable;
     new->writeable = writeable;
     new->writeable_prev = writeable;
     new->executable = executable;
 
	new -> next = as -> as_regions;
	as -> as_regions = new;
    //  new->next = as->as_regions;
    //  as->as_regions = new;
     
     (void)as;
     (void)vaddr;
     (void)memsize;
     (void)readable;
     (void)writeable;
     (void)executable;
     return 0;
 }
 
 int
 as_prepare_load(struct addrspace *as)
 {
     /*
      * Write this.
      */
     struct region *curr = as->as_regions;
     while (curr != NULL) { // change readonly to rw
         // curr->writeable_prev = curr->writeable;
         curr->writeable = 1;
         curr = curr->next;
     }
 
     (void)as;
     return 0;
 }
 
 int
 as_complete_load(struct addrspace *as)
 {
     /*
      * Write this.
      */
     if (as == NULL) {
         return EFAULT;
     }
     struct region *curr = as->as_regions;
     while (curr != NULL) {
         // set permissions back to old one
         curr->writeable = curr->writeable_prev;
         curr = curr->next;
     }
     int spl = splhigh();
     for (int i = 0; i<NUM_TLB; i++) {
         tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
     } 
     splx(spl);
     (void)as;
     return 0;
 }
 
 int
 as_define_stack(struct addrspace *as, vaddr_t *stackptr)
 {
     /*
      * Write this.
      */
     if (as == NULL) {
         return EFAULT;
     }
     (void)as;
 
     /* Initial user-level stack pointer */
     *stackptr = USERSTACK;
     // read write to 1, exectuable to 0
     return as_define_region(as, *stackptr - USER_STACK_SIZE, USER_STACK_SIZE, 1, 1, 0);
 }
 