/* 
 *  This module tries to erase as much RAM as possible, 
 *  before killing itself.
 *  Machine will be left in undefined state, 
 *  but you should be able to reset it over IPMI.
 *  
 *  Under GNU GPL
 *
 *  2010, niekt0@hysteria.sk
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/raw.h>
#include <linux/ptrace.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/splice.h>
#include <linux/pfn.h>
#include <linux/reboot.h>
#include <asm/reboot.h>

#include "debug.h"

#ifdef CONFIG_IA64
# include <linux/efi.h>
#endif

#define KB_4		4096	

#ifdef ARCH_x86_64
#define NEST_PAGES 	5	// how big is the nest (page tables + asm) x86_64
#else
#define NEST_PAGES 	3	// how big is the nest (page tables + asm)
#endif

#define	MAX_DEL_AREAS	32	// how big is field with areas to delete. ( start+end, => div 2 )a
#define JMPADDR 	0x2000	// static address where to jump

#ifdef ARCH_i386	// 4MB mapping used
#define ARGADDR		(NEST_PAGES*KB_4-MAX_DEL_AREAS*sizeof(unsigned long))	// in new page tables
#endif

#ifdef ARCH_x86_64	// 4KB mapping used
#define ARGADDR         (JMPADDR+KB_4-MAX_DEL_AREAS*sizeof(unsigned long))   // in new page tables
#endif


#ifdef ARCH_i386
#define MY_PAGE_SHIFT	12	// on i386 we need to shift addresses 12, to be able to access over 4GB
#else
#define MY_PAGE_SHIFT	0	// on other architectures this is not needed
#endif


MODULE_LICENSE("GPL");

// Char we show before each debug print
const char program_name[] = "alzheimer";


/* This function is re-implemented, 
 * because older kernels does not have one.
 * 
 * Add "System RAM" type entries from iomem_resource 
 * to our array.
 *
 */
static int my_find_next_system_ram(struct resource *res,unsigned long *memareas,unsigned long nest_physical)
{
        resource_size_t start, end;
        struct resource *p;
	int tmp1=0;

        BUG_ON(!res);

        start = res->start;
        end = res->end;
        BUG_ON(start >= end);

//        read_lock(&resource_lock); //problem on some kernels. We are dirty.
        for (p = iomem_resource.child; p ; p = p->sibling) {
                /* system ram is just marked as IORESOURCE_MEM */
//		dbgprint("%X-%X : %s",p->start,p->end,p->name);
		if ((strncmp("System RAM",p->name,10))==0){
			dbgprint("adding %lX-%lX : %s, [%d]",(unsigned long)p->start,(unsigned long)p->end,p->name,tmp1);
			// XXX TODO test on PAE system with enough RAM
		
			// on i386, all addresses are shifted 12 to the right ("divided")
			// this is needed. if we want to delete addresses over 4GB
			// on i386 architecture. (for example when PAE is used) 
			// so our "units" are 4KB blocks
			memareas[tmp1++]=(p->start >>MY_PAGE_SHIFT); 
			// Exception, for our own code
			if ((p->start <= nest_physical) && \
			    (p->end >= nest_physical)) {
				dbgprint("Splitting area 0x%lX-0x%lX",(unsigned long)p->start,(unsigned long)p->end);
				memareas[tmp1++]=((nest_physical-1) >>MY_PAGE_SHIFT);	//end of first area
				memareas[tmp1++]=((nest_physical+KB_4*NEST_PAGES) >>MY_PAGE_SHIFT);
				// beginning of second area (minus NEST_PAGES pages)
				// this need to be changed if post-jump area is expanded
			}
			memareas[tmp1++]=(p->end >> MY_PAGE_SHIFT);
		}

                if (p->start > end) {
                        p = NULL;
                        break;
                }
        }

//        read_unlock(&resource_lock);
        if (!p)
                return -1;
        /* copy data */
        if (res->start < p->start)
                res->start = p->start;
        if (res->end > p->end)
                res->end = p->end;
        return 0;
}

/*
 * Main function of whole program.
 * Everything is prepared here,
 * and at the end program jump 
 * to minimal assembler routine 
 * with own page tables.
 *
 * - disable all the interrupts,
 * - create fake page tables
 * - copy asm routine close to tables
 * - copy RAM ranges close to asm routine
 * - set new tables and jump to asm routine
 *
 */

int start_cleaning(void){
	
	int tmp2=0;
	unsigned long tmp_long=0;
	unsigned long nest=0;
	unsigned long nest_physical=0;
	unsigned long memareas[MAX_DEL_AREAS];
	extern void eraser_start(void);
	extern void eraser_end(void);

	dbgprint("Cleaning:..");
	
	//Just to make it nicer when debugging
	for (tmp2=0;tmp2<MAX_DEL_AREAS;){
		memareas[tmp2++]=0;
	}


	// Allocate NEST_PAGES pages somewhere,
	// where we will survive the mess
	nest=__get_free_pages(GFP_KERNEL,NEST_PAGES); //XXX 64
	// See include/linux/gfp.h in kernel for details

	nest_physical=virt_to_phys((void *)nest);
	//so we can set up page tables properly


	// Kill all other CPUs
	// This part is ripped from x86_shutdown

	// This function is problematic
	// In new kernels seems to be exported,
	// but there are still some problems. (#ifdef ?)
	// need to be tested on multicpu system

//	nmi_shootdown_cpus((void *)dummy_fc);

	// get memory maps (same as /proc/iomem)
	my_find_next_system_ram(&iomem_resource,memareas,nest_physical);

	dbgprint("nest: 0x%lX",nest);
	dbgprint("Memareas:");
	for (tmp2=0;tmp2<MAX_DEL_AREAS;){
		dbgprint(" 0x%lX",memareas[tmp2++]);
	}

	// fake page_table structure (i386)
	// 4KB non-PAE non-PSE mode is used (2 levels of tables)
	// mixed with 4MB non-PAE PSE mode (only 1 level)
	// We ourselves will be in 4KB page (because of alignment)
	// Working page will be in 4MB PSE mode (so we can access over 4GB)
	// see 24593.pdf from AMD for more info
	
	memset((void *)nest,0,KB_4*NEST_PAGES); //4 KB, NEST_PAGES pages
	
	// copy PGD from original system PGD.
	// This is better, because everything important will stay mapped.
	tmp_long=(unsigned long) phys_to_virt(read_cr3());
	memcpy((void *)nest,(void *)tmp_long,KB_4);


	// Construct own page tables

#ifdef ARCH_i386
	//((nest address + 1 4KB page)=PTE phys addr) + bits
	tmp_long=((nest_physical+KB_4)&0xFFFFF000)+0xF7F;
	memcpy((void *)nest,&tmp_long,sizeof(tmp_long));	// to make 0x0 PGD itself (PGD)
	tmp_long=(nest_physical&0xFFFFF000)+0x3F;		// nest address + bits
	memcpy((void *)nest+KB_4,&tmp_long,sizeof(tmp_long));	// to make 0x0 PGD itself (PT)
	tmp_long=((nest_physical+KB_4*(NEST_PAGES-1))&0xFFFFF000)+0x3F; //nest address + 2 pages + bits (ie. the page with code itself)
	memcpy((void *)nest+KB_4+2*sizeof(unsigned long),&tmp_long,sizeof(tmp_long)); //to map 0x2000 to page with code + ranges

	memcpy((void *)nest+KB_4*(NEST_PAGES-1),eraser_start,eraser_end-eraser_start);// erasing code itself
	memcpy((void *)nest+KB_4*NEST_PAGES-(sizeof(unsigned long)*MAX_DEL_AREAS),memareas,sizeof(unsigned long)*MAX_DEL_AREAS);
	// up to MAX_DEL_AREAS areas is enough for everyone;)
	
	// so code should be placed at 0x2000 (2*4K)
	// arguments should be at 0x2ea0 (3*4K - 64*4)

	// To prevent EIP from containing invalid pointer after table switch (this would cause triple-fault)
	// So EIP must be valid in new tables too.
	// 4MB page used
#endif	
#ifdef ARCH_x86_64
	// ((nest address + 1 4KB page)=PTE phys addr) + bits
	tmp_long=((nest_physical+KB_4)&0xFFFFFFFFFF000)+0x01F;	// bits 51-12 as address, +flag bits (see doc)
	memcpy((void *)nest,&tmp_long,sizeof(tmp_long));	// to make virt 0x0 PTE itself (PML4)
	tmp_long=((nest_physical+KB_4*2)&0xFFFFFFFFFF000)+0x01F;// nest address + bits
	memcpy((void *)nest+KB_4,&tmp_long,sizeof(tmp_long));	// to make virt 0x0 PTE itself (PDPE)
	tmp_long=((nest_physical+KB_4*3)&0xFFFFFFFFFF000)+0x01F;// nest address + bits
	memcpy((void *)nest+KB_4*2,&tmp_long,sizeof(tmp_long));	// to make virt 0x0 PTE itself (PDE)
	tmp_long=((nest_physical+KB_4*3)&0xFFFFFFFFFF000)+0x01F;// nest address + bits
	memcpy((void *)nest+KB_4*3,&tmp_long,sizeof(tmp_long));	// to make 0x0 PTE itself (PTE)

//	tmp_long=((nest_physical)&0xFFFFFFFFFF000)+0x01F;	// nest address + bits
//	memcpy((void *)nest+KB_4*3+sizeof(unsigned long),&tmp_long,sizeof(tmp_long));	// to make 0x1000 PTE itself (PTE)



	tmp_long=((nest_physical+KB_4*(NEST_PAGES-1))&0xFFFFFFFFFF000)+0x01F;
	// nest address + pages + bits 
	// (ie. the page with code itself)
	memcpy((void *)nest+KB_4*3+2*sizeof(unsigned long),&tmp_long,sizeof(tmp_long)); 
	// to map 0x2000 to page 
	// with code + ranges (PTE, rest of tables is recycled)

	memcpy((void *)nest+KB_4*(NEST_PAGES-1),eraser_start,eraser_end-eraser_start);// erasing code itself
	memcpy((void *)nest+KB_4*NEST_PAGES-(sizeof(unsigned long)*MAX_DEL_AREAS),memareas,sizeof(unsigned long)*MAX_DEL_AREAS);
	// up to MAX_DEL_AREAS areas is enough for everyone;)
	
	// so code should be placed at 0x4000 ((5-1)*4K)
	// arguments should be at 0x4ea0 (5*4K - 32*8)

	// To prevent EIP from containing invalid pointer after table switch (this would cause triple-fault)
	// So EIP must be valid in new tables too.
#endif	

	dbgprint("Arguments at: 0x%lX ",(unsigned long)ARGADDR);

	dbgprint("after memset: nest: 0x%lX, nest_phys: 0x%lx",nest,nest_physical);
	
	// disable interrupts 
	__asm ("cli\n\t");

	//Everything is prepared.
	//Just set register and jump now.
	//Good luck!

#ifdef ARCH_i386
	__asm (		
	"mov %%cr4,%%edx\n\t"		// cannot write directly
	"or $0x00000010,%%edx\n\t"      // enable PSE, bit number 4 (because wee need 4MB page)
	"mov %%edx,%%cr4\n\t"		// cannot write directly
	"mov %%eax,%%edx\n\t"		// tmp, stack will be destroyed later. (see last line)
	"mov %%edx,%%cr3\n\t"		// new page table address
	"mov %%cr4,%%edx\n\t"		// cannot write directly
	"and $0xFFFFFFDF,%%edx\n\t"	// disable PAE, bit number 5
	"mov %%edx,%%cr4\n\t"		// cannot write directly
	"mov $0x2000,%%edx\n\t"		// cannot jump directly 
	"invlpg 0x2000\n\t"		// flush TLB 
	"mov %0,%%ebp\n\t"		// 0x2ea0 = 3*4KB-0x160 in our new tables. Good bye stack!
	"jmp *%%edx\n\t"		// JUMP!
	:
	:"i"(ARGADDR),"a"(nest_physical)	// %0 + eax 
	);

#endif

#ifdef ARCH_x86_64

	__asm (		// XXX 64
	"mov %%rax,%%rdx\n\t"		// tmp, stack will be destroyed later. (see last line)
	"mov $0xFFFFFFFFFF000, %%rbx\n\t"	// hack, cannot and directly
	"and %%rbx,%%rdx\n\t"		// only bites 51-12(so this address is 4KB aligned)
	"mov %%rdx,%%cr3\n\t"		// new page table address
	"mov $0x2000,%%rdx\n\t"		// cannot jump directly 
	"invlpg 0x2000\n\t"		// flush TLB 
	"mov %0,%%rbp\n\t"		// Set argument address. Good bye stack!
	"jmp *%%rdx\n\t"		// JUMP!
	:
	:"i"(ARGADDR),"a"(nest_physical)	// %0 + rax 
	);

#endif

#ifdef ARCH_arm

//XXX TODO arm

#endif

	// This point should not be reached
	return 0;

}


/// Function which is executed upon loading this module to kernel
int __init
init_module (void)
{

	dbgprint("init");	

	//no one is going to survive here
	start_cleaning();  

	return 0;
}

/// Function which will be executed upon unloading this module to kernel		
void __exit
cleanup_module (void)
{
	// This should never happen
	// (unless we survived own death;)
	dbgprint ("WTF? Unloading...");

}
