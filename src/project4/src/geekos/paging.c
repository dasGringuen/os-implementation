/*
 * Paging (virtual memory) support
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.55 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/string.h>
#include <geekos/int.h>
#include <geekos/idt.h>
#include <geekos/kthread.h>
#include <geekos/kassert.h>
#include <geekos/screen.h>
#include <geekos/mem.h>
#include <geekos/malloc.h>
#include <geekos/gdt.h>
#include <geekos/segment.h>
#include <geekos/user.h>
#include <geekos/vfs.h>
#include <geekos/crc32.h>
#include <geekos/paging.h>

/* ----------------------------------------------------------------------
 * Public data
 * ---------------------------------------------------------------------- */
    
void* g_kernelPageDir;

/* ----------------------------------------------------------------------
 * Private functions/data
 * ---------------------------------------------------------------------- */

#define SECTORS_PER_PAGE (PAGE_SIZE / SECTOR_SIZE)

/*
 * flag to indicate if debugging paging code
 */
int debugFaults = 0;
#define Debug(args...) if (debugFaults) Print(args)


void checkPaging()
{
  unsigned long reg=0;
  __asm__ __volatile__( "movl %%cr0, %0" : "=a" (reg));
  Print("Paging on ? : %d\n", (reg & (1<<31)) != 0);
}


/*
 * Print diagnostic information for a page fault.
 */
static void Print_Fault_Info(uint_t address, faultcode_t faultCode)
{
    extern uint_t g_freePageCount;

    Print("Pid %d, Page Fault received, at address %x (%d pages free)\n",
        g_currentThread->pid, address, g_freePageCount);
    if (faultCode.protectionViolation)
        Print ("   Protection Violation, ");
    else
        Print ("   Non-present page, ");
    if (faultCode.writeFault)
        Print ("Write Fault, ");
    else
        Print ("Read Fault, ");
    if (faultCode.userModeFault)
        Print ("in User Mode\n");
    else
        Print ("in Supervisor Mode\n");
}

/*
 * Handler for page faults.
 * You should call the Install_Interrupt_Handler() function to
 * register this function as the handler for interrupt 14.
 */
/*static*/ void Page_Fault_Handler(struct Interrupt_State* state)
{
    ulong_t address;
    faultcode_t faultCode;

    KASSERT(!Interrupts_Enabled());

    /* Get the address that caused the page fault */
    address = Get_Page_Fault_Address();
    Debug("Page fault @%lx\n", address);

    /* Get the fault code */
    faultCode = *((faultcode_t *) &(state->errorCode));

    /* rest of your handling code here */
    Print ("Unexpected Page Fault received\n");
    Print_Fault_Info(address, faultCode);
    Dump_Interrupt_State(state);
    /* user faults just kill the process */
    if (!faultCode.userModeFault) KASSERT(0);

    /* For now, just kill the thread/process. */
    Exit(-1);
}

/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

/* used to get the physical address of the table */
ulong_t Get_Table_Page_Address( ulong_t dirEntrySelector)
{
    return 0;
}


void Set_Page_Table_Entry(
    pte_t* tableEntry, 
    ulong_t physicalAddr,
    ulong_t flags 
)
{
  //  Print("Table Entry  %X", (unsigned int)tableEntry);
  //  Print("Base address: %X\n", (unsigned int)physicalAddr);
    tableEntry->present = 1;                     /* 1b               */
    tableEntry->flags = flags & 0x0F;            /* 4b               */
    tableEntry->accesed = 0;                     /* 1b               */
    tableEntry->dirty = 0;                       /* 1b               */
    tableEntry->pteAttribute = 0;                /* 1b               */
    tableEntry->globalPage = 0;                  /* 1b               */
    tableEntry->kernelInfo = 0; //????????       /* 3b               */ 
    tableEntry->pageBaseAddr = (physicalAddr >> PAGE_POWER) & 0xFFFFF;    /* 20b              */
}

void Set_Page_Directory_Entry(
    pde_t* dirEntry,
    ulong_t physicalAddr,
    ulong_t flags 
)
{
    Print("Dir Entry address: %X", (unsigned int)dirEntry);
    Print("Base address: %X\n", (unsigned int)physicalAddr);
    dirEntry->present = 1;                     /* 1b               */
    dirEntry->flags = flags & 0x0F;            /* 4b               */
    dirEntry->accesed = 0;                     /* 1b               */
    //directory->reserved                      /* 1b               */
    dirEntry->largePages = 0;                  /* 1b               */
    dirEntry->globalPage = 0;                  /* 1b               */
    dirEntry->kernelInfo = 0; //????????       /* 3b               */ 
    dirEntry->pageTableBaseAddr = (physicalAddr >> PAGE_POWER ) & 0xFFFFF;    /* 20b              */
}



void* Register_User_Page( pde_t* pageDir, 
        ulong_t vaddr, 
        ulong_t flags)
{
    pte_t* entry = Register_Page(
                        pageDir,        /* page directory */ 
                        vaddr,          /* linear address */ 
                        flags);
                        
                /* alloc a page, the return value will be the physical address */
                void* phaddr = Alloc_Pageable_Page(entry, vaddr);
            
                /* fill the table entry with the physical address */
                Set_Page_Table_Entry(entry, (ulong_t)phaddr, flags);

                return phaddr;
}

/* 
 * @param pageDirectory: in bytes
 * @param linearAddr: in BYTES not in Pages
 * @param flags  
 * @return a pointer to the table written. 
 */
pte_t* Register_Page(
    pde_t* pageDirectory, 
    ulong_t linearAddr, 
    ulong_t flags 
)
{
    ulong_t dirIndex;
    ulong_t tableIndex;
    pde_t* dirEntry;
    void* tablePhyAddress;
    pte_t* tableEntry;

    /* get the index of the linear address on the directory table */
    dirIndex = PAGE_DIRECTORY_INDEX( linearAddr );

    /* get the index on the table */
    tableIndex = PAGE_TABLE_INDEX( linearAddr );

    /*Print("LinearAddress:%X, DirIndex:%X, TableInd:%X ", (int)linearAddr, (int)dirIndex, (int)tableIndex); */

    /* check if the directory entry for this address exist */
    dirEntry = &pageDirectory[dirIndex];

    if(!dirEntry->present)
    {
        /* I it does not exist, allocate a new table and add a entry on the
         * directory table 
         */
        tablePhyAddress = Alloc_Page();
        memset(tablePhyAddress, '\0',PAGE_SIZE);
        Print("New Table: %.8lx\n", (ulong_t)tablePhyAddress);

        Set_Page_Directory_Entry(dirEntry, 
                (ulong_t)tablePhyAddress,
                flags);                /* flags */

    } else {
        tablePhyAddress = (void*)(dirEntry->pageTableBaseAddr << 12); 
    }

    /* Print("Using Table: %.8lx\n", (ulong_t)tablePhyAddress); */

    /* creates an entry on the table, now will be pointig to the beggining of
     * the table */
    tableEntry = (pte_t*)tablePhyAddress;
  
    /* returns a pointer to the entry on the TABLE not the DIR!! */
    return (pte_t*)(tableEntry + tableIndex);
}



/*
 * Initialize virtual memory by building page tables
 * for the kernel and physical memory.
 */
void Init_VM(struct Boot_Info *bootInfo)
{
    /*
     * Hints:
     * - Build kernel page directory and page tables
     * - Call Enable_Paging() with the kernel page directory
     * - Install an interrupt handler for interrupt 14,
     *   page fault
     * - Do not map a page at address 0; this will help trap
     *   null pointer references
     */
    int i;
  
    /* calculate number of pages */
    ulong_t numPages = bootInfo->memSizeKB >> 2;
   
    /* page directory */
    g_kernelPageDir = Alloc_Page();
    memset(g_kernelPageDir, '\0', PAGE_SIZE);

    Print("Directroy on:%.8lx\n",(ulong_t)g_kernelPageDir);

/*
 * Get the address of the page, from the index of .
 */
    for (i=0; i < numPages; i++) {
        ulong_t address = i << PAGE_POWER; 
        ulong_t flags = VM_WRITE | VM_READ ;//| VM_USER;

        pte_t *entry = Register_Page(
                g_kernelPageDir, 
                address,                /* virtual address */
                flags);

        /* set the table entry with the physical address */
        Set_Page_Table_Entry(entry, address, flags); 

        /* get the struct of the page, (is in the queue of all pages) */
        struct Page* tPage = Get_Page(address);
        /* set the table entry (entry of the table not the directory) */
        tPage->entry = entry;
        /* set the virtual address, for kernel is the same as the physical */
        tPage->vaddr = address;
    }
    
    Install_Interrupt_Handler(14, Page_Fault_Handler);
     
    Enable_Paging(g_kernelPageDir);
    return;

#if 0
    for (i=0; i < kernelDirEntries; i++) {
        void* kernelTable = Alloc_Page();

        /* add entry on the dir table */
        pde_t dirEntry =  
        /* calculates number of entries in the table */
        ulong_t pagesInTable = (numPages <= 1024) ? numPages: 1024;

            Print("%d", (int)pagesInTable);
        for (j = 0; j < pagesInTable; j++) {
            /* add entry on the table */ 
            //Print("%d", (int)numPages);
            numPages--; 
        }
    }
#endif

#if 0
        if ((g_pageList[i].flags & PAGE_PAGEABLE) &&
                (g_pageList[i].flags & PAGE_ALLOCATED)) {
            if (!best) best = &g_pageList[i];
            curr = &g_pageList[i];
            if ((curr->clock < best->clock) && (curr->flags & PAGE_PAGEABLE)) {
                best = curr;
            }
        }
    }

PAGE_DIRECTORY_INDEX
#endif
    //TODO("Build initial kernel page directory and page tables");
}

/**
 * Initialize paging file data structures.
 * All filesystems should be mounted before this function
 * is called, to ensure that the paging file is available.
 */
void Init_Paging(void)
{
  //  TODO("Initialize paging file data structures");
}

/**
 * Find a free bit of disk on the paging file for this page.
 * Interrupts must be disabled.
 * @return index of free page sized chunk of disk space in
 *   the paging file, or -1 if the paging file is full
 */
int Find_Space_On_Paging_File(void)
{
    KASSERT(!Interrupts_Enabled());
    TODO("Find free page in paging file");
}

/**
 * Free a page-sized chunk of disk space in the paging file.
 * Interrupts must be disabled.
 * @param pagefileIndex index of the chunk of disk space
 */
void Free_Space_On_Paging_File(int pagefileIndex)
{
    KASSERT(!Interrupts_Enabled());
    TODO("Free page in paging file");
}

/**
 * Write the contents of given page to the indicated block
 * of space in the paging file.
 * @param paddr a pointer to the physical memory of the page
 * @param vaddr virtual address where page is mapped in user memory
 * @param pagefileIndex the index of the page sized chunk of space
 *   in the paging file
 */
void Write_To_Paging_File(void *paddr, ulong_t vaddr, int pagefileIndex)
{
    struct Page *page = Get_Page((ulong_t) paddr);
    KASSERT(!(page->flags & PAGE_PAGEABLE)); /* Page must be locked! */
    TODO("Write page data to paging file");
}

/**
 * Read the contents of the indicated block
 * of space in the paging file into the given page.
 * @param paddr a pointer to the physical memory of the page
 * @param vaddr virtual address where page will be re-mapped in
 *   user memory
 * @param pagefileIndex the index of the page sized chunk of space
 *   in the paging file
 */
void Read_From_Paging_File(void *paddr, ulong_t vaddr, int pagefileIndex)
{
    struct Page *page = Get_Page((ulong_t) paddr);
    KASSERT(!(page->flags & PAGE_PAGEABLE)); /* Page must be locked! */
    TODO("Read page data from paging file");
}

