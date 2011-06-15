/*
 * Paging-based user mode implementation
 * Copyright (c) 2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.50 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/int.h>
#include <geekos/mem.h>
#include <geekos/paging.h>
#include <geekos/segment.h>
#include <geekos/gdt.h>
#include <geekos/malloc.h>
#include <geekos/string.h>
#include <geekos/argblock.h>
#include <geekos/kthread.h>
#include <geekos/range.h>
#include <geekos/vfs.h>
#include <geekos/user.h>

/* ----------------------------------------------------------------------
 * Private functions
 * ---------------------------------------------------------------------- */

static bool Validate_User_Memory(struct User_Context* userContext,
    ulong_t userAddr, ulong_t bufSize)
{

    ulong_t avail;

    if (userAddr >= USER_VM_SIZE)
        return false;

    avail = USER_VM_SIZE - userAddr;
    if (bufSize > avail)
        return false;

    return true;
}

/*
 * Create a new user context of given size
 */
static struct User_Context* Create_User_Context(ulong_t size)
{
    /* Tiene que ser múltiplo de PAGE_SIZE */
    KASSERT(size%PAGE_SIZE == 0);

    /* Pido memoria para el proceso */
    char *mem = (char *) Malloc(size);
    if (mem == NULL)
        goto error;

    /* Reset memory with zeros */
    memset(mem, '\0', size);
    
    /* Pido memoria para el User_Context */
    struct User_Context *userContext = Malloc(sizeof(struct User_Context));
    if (userContext ==  NULL)
        goto error;

    /* Guardo el segment descriptor de la ldt en la gdt */
    struct Segment_Descriptor *ldt_desc = Allocate_Segment_Descriptor(); //LDT-Descriptor for the process
    if (ldt_desc == NULL)
        goto error;

    Init_LDT_Descriptor(ldt_desc, userContext->ldt, NUM_USER_LDT_ENTRIES);
    /* Creo un selector para el descriptor de ldt */
    ushort_t ldt_selector = Selector(KERNEL_PRIVILEGE, true, Get_Descriptor_Index(ldt_desc)); 

    /* TODO ver esto!!! los limites*/
    Init_Code_Segment_Descriptor(&(userContext->ldt[0]),  USER_VM_START, USER_VM_SIZE / PAGE_SIZE, USER_PRIVILEGE);
    Init_Data_Segment_Descriptor(&(userContext->ldt[1]),  USER_VM_START, USER_VM_SIZE / PAGE_SIZE, USER_PRIVILEGE);
    //Init_Code_Segment_Descriptor(&(userContext->ldt[0]), (ulong_t)mem, size/PAGE_SIZE, USER_PRIVILEGE);
    //Init_Data_Segment_Descriptor(&(userContext->ldt[1]), (ulong_t)mem, size/PAGE_SIZE, USER_PRIVILEGE);

    /* Creo los selectores */
    ushort_t cs_selector = Selector(USER_PRIVILEGE, false, 0);
    ushort_t ds_selector = Selector(USER_PRIVILEGE, false, 1);


    /* Asigno todo al userContext */
    userContext->ldtDescriptor = ldt_desc;
    userContext->ldtSelector = ldt_selector;
    userContext->csSelector = cs_selector;
    userContext->dsSelector = ds_selector;
    userContext->size = size;
    userContext->memory = mem;
    userContext->refCount = 0;
    goto success;

error:
    if (mem != NULL)
        Free(mem);
    if (userContext != NULL)
        Free(userContext);
    return NULL;

success:
    return userContext;
}
/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

/*
 * Destroy a User_Context object, including all memory
 * and other resources allocated within it.
 */
void Destroy_User_Context(struct User_Context* context)
{
    /*
     * Hints:
     * - Free all pages, page tables, and page directory for
     *   the process (interrupts must be disabled while you do this,
     *   otherwise those pages could be stolen by other processes)
     * - Free semaphores, files, and other resources used
     *   by the process
     */
    TODO("Destroy User_Context data structure after process exits");
}

/*
 * Load a user executable into memory by creating a User_Context
 * data structure.
 * Params:
 * exeFileData - a buffer containing the executable to load
 * exeFileLength - number of bytes in exeFileData
 * exeFormat - parsed ELF segment information describing how to
 *   load the executable's text and data segments, and the
 *   code entry point address
 * command - string containing the complete command to be executed:
 *   this should be used to create the argument block for the
 *   process
 * pUserContext - reference to the pointer where the User_Context
 *   should be stored
 *
 * Returns:
 *   0 if successful, or an error code (< 0) if unsuccessful
 */
int Load_User_Program(char *exeFileData, ulong_t exeFileLength,
    struct Exe_Format *exeFormat, const char *command,
    struct User_Context **pUserContext)
{
    /*
     * Hints:
     * - This will be similar to the same function in userseg.c
     * - Determine space requirements for code, data, argument block,
     *   and stack
     * - Allocate pages for above, map them into user address
     *   space (allocating page directory and page tables as needed)
     * - Fill in initial stack pointer, argument block address,
     *   and code entry point fields in User_Context
     */

    KASSERT(exeFileData != NULL);
    KASSERT(command != NULL);
    KASSERT(exeFormat != NULL);

    int i;
    int ret = 0;
    ulong_t maxva = 0;
    ulong_t flags = VM_WRITE | VM_READ | VM_USER;
    ulong_t virtSize;


    void* vaddr;
    ulong_t argBlockSize = 0;
    ulong_t argVirAdd = 0;
    void* argPhyAdd = 0;

    void* stackVirAdd = 0;
    void* stackPhyAdd = 0;

    //ulong_t stackAddr = 0;
    unsigned numArgs = 0;
    struct User_Context *userContext = 0;

    /* Page directory for user address space. */
    pde_t *pageDir = Alloc_Page();
    memset(pageDir, '\0', PAGE_SIZE);
 
    Print("User directroy on:%.8lx\n",(ulong_t)pageDir);

    /* copy the entries form the kernel page directory */
    memcpy(pageDir, g_kernelPageDir, PAGE_SIZE);
 
    /* Find maximum virtual address */
    for (i = 0; i < exeFormat->numSegments; ++i) {
        struct Exe_Segment *segment = &exeFormat->segmentList[i];
        ulong_t topva = segment->startAddress + segment->sizeInMemory;

        if (topva > maxva)
            maxva = topva;
    }

    /* get arguments */
    Get_Argument_Block_Size(command, &numArgs, &argBlockSize);
    
    /* user context */
    virtSize = Round_Up_To_Page(maxva ) + 0x2000;
    userContext = Create_User_Context(virtSize);

    /* Copy segments over into process' memory space */
    for (i = 0; i < exeFormat->numSegments; i++) {
        struct Exe_Segment *segment = &exeFormat->segmentList[i];
        
        memcpy(userContext->memory + segment->startAddress,
            exeFileData + segment->offsetInFile,
            segment->lengthInFile);
    }

    /* fill the user pages */
    int nPages = virtSize / PAGE_SIZE + 1;

    Print("nPages:%d", nPages);
    Print(" tempMem: %p\n", userContext->memory);
    vaddr = (void*)USER_VM_START;
    Print(" Virtual Add:%lX ", (ulong_t)vaddr);

    for (i = 0; i < nPages; i++) {
        void* phaddr;

        phaddr = Register_User_Page(pageDir, (ulong_t)vaddr + i * PAGE_SIZE, flags);
        Print(" PhPage:%lX ", (ulong_t)phaddr );

        /* copy the file content on the new created page */
        Print("Coping. From:%lx To:%lx \n", (ulong_t)userContext->memory + i * PAGE_SIZE,
                                                    (ulong_t)phaddr);

        memcpy( (char*)phaddr, userContext->memory + i * PAGE_SIZE, PAGE_SIZE);
    }

    /* STACK */
    /* register the virtual address */
    stackVirAdd = (void*)0xFFFFE000;
    stackPhyAdd = Register_User_Page(pageDir, (ulong_t)stackVirAdd, flags);
    memset(stackPhyAdd, '\0', PAGE_SIZE);

    /* ARGS */
    argVirAdd = 0xFFFFF000;
    argPhyAdd = Register_User_Page(pageDir, argVirAdd, flags);

    /* format the args */
    Format_Argument_Block(argPhyAdd,
            numArgs,
            argVirAdd - USER_VM_START, /* must be in user space */
            command);

    /* Create the user context */
    userContext->entryAddr          = exeFormat->entryAddr;
    userContext->argBlockAddr       = argVirAdd - USER_VM_START;//virtSize - 0x100;//argVirAdd - USER_VM_START;
    userContext->stackPointerAddr   = (ulong_t)((ulong_t)stackVirAdd - USER_VM_START + PAGE_SIZE );

    Print("Stack Physical:%lx,virtual:%lx,user:%lx \n",(ulong_t)stackPhyAdd, (ulong_t)stackVirAdd ,userContext->stackPointerAddr);

    userContext->pageDir = pageDir;
    *pUserContext = userContext;
    return ret;
}

/*
 * Copy data from user buffer into kernel buffer.
 * Returns true if successful, false otherwise.
 */
bool Copy_From_User(void* destInKernel, ulong_t srcInUser, ulong_t bufSize)
{
    /*
     * Hints:
     * - Make sure that user page is part of a valid region
     *   of memory
     * - Remember that you need to add 0x80000000 to user addresses
     *   to convert them to kernel addresses, because of how the
     *   user code and data segments are defined
     * - User pages may need to be paged in from disk before being accessed.
     * - Before you touch (read or write) any data in a user
     *   page, **disable the PAGE_PAGEABLE bit**.
     *
     * Be very careful with race conditions in reading a page from disk.
     * Kernel code must always assume that if the struct Page for
     * a page of memory has the PAGE_PAGEABLE bit set,
     * IT CAN BE STOLEN AT ANY TIME.  The only exception is if
     * interrupts are disabled; because no other process can run,
     * the page is guaranteed not to be stolen.
     */

    KASSERT(destInKernel != NULL);
    KASSERT(srcInUser != 0);

    bool mem_valid = false;

    if (g_currentThread->userContext != NULL) {
        mem_valid = Validate_User_Memory(g_currentThread->userContext,
                                            srcInUser, bufSize);
        if (mem_valid) {
                memcpy(destInKernel,
                        (char*)(srcInUser + 0x80000000), 
                        bufSize);
        }
    }
    return mem_valid;
}

/*
 * Copy data from kernel buffer into user buffer.
 * Returns true if successful, false otherwise.
 */
bool Copy_To_User(ulong_t destInUser, void* srcInKernel, ulong_t numBytes)
{
    /*
     * Hints:
     * - Same as for Copy_From_User()
     * - Also, make sure the memory is mapped into the user
     *   address space with write permission enabled
     */

    KASSERT(srcInKernel != NULL);
    KASSERT(destInUser != 0);
    
    bool mem_valid = true;
    
    if (g_currentThread->userContext != NULL) {
        mem_valid = Validate_User_Memory(g_currentThread->userContext,
                                            destInUser, numBytes);
        if (mem_valid) {
            memcpy((char*)(0x80000000+destInUser), srcInKernel,numBytes);
        }
    }
    return mem_valid;
}

/*
 * Switch to user address space.
 */
void Switch_To_Address_Space(struct User_Context *userContext)
{
    /*
     * - If you are still using an LDT to define your user code and data
     *   segments, switch to the process's LDT
     * - 
     */
    KASSERT(userContext != NULL);
    KASSERT(userContext->ldtSelector != 0);
    /*
     * Hint: you will need to use the lldt assembly language instruction
     * to load the process's LDT by specifying its LDT selector.
     */
    /* Load the task register */
    __asm__ __volatile__ (
        "lldt %0"
        :
        : "a" (userContext->ldtSelector)
    );


    Set_PDBR(userContext->pageDir);
}


