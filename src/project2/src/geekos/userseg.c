/*
 * Segmentation-based user mode implementation
 * Copyright (c) 2001,2003 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.23 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/ktypes.h>
#include <geekos/kassert.h>
#include <geekos/defs.h>
#include <geekos/mem.h>
#include <geekos/string.h>
#include <geekos/malloc.h>
#include <geekos/int.h>
#include <geekos/gdt.h>
#include <geekos/segment.h>
#include <geekos/tss.h>
#include <geekos/kthread.h>
#include <geekos/argblock.h>
#include <geekos/user.h>

/* ----------------------------------------------------------------------
 * Variables
 * ---------------------------------------------------------------------- */

#define DEFAULT_USER_STACK_SIZE 8192


/* ----------------------------------------------------------------------
 * Private functions
 * ---------------------------------------------------------------------- */

/*----------------------------------------------------------------------------*/
void memDump(const void * src, size_t length);
void memDump(const void * src, size_t length)
{
	char* address = (char*)src;
	int i = 0; //used to keep track of line lengths
	char *line = (char*)address; //used to print char version of data
	unsigned char ch; // also used to print char version of data
		
	Print("%p| ", address); //Print the address we are pulling from
	while (length-- > 0) {
		Print("%02X ", (unsigned char)*address++); //Print each char
		if (!(++i % 16) || (length == 0 && i % 16)) { //If we come to the end of a line...
			//If this is the last line, print some fillers.
			if (length == 0) { while (i++ % 16) { Print("__ "); } }
			Print("| ");
			while (line < address) {  // Print the character version
				ch = *line++;
				Print("%c", (ch < 33 || ch == 255) ? 0x2E : ch);
			}
			// If we are not on the last line, prefix the next line with the address.
			if (length > 0) { Print("\n%p| ", address); }
		}
	}
	Print("\n");
}

/*
 * Create a new user context of given size
 *
 	TODO 
 
int fun(int a)
 405{
 406        int result = 0;
 407        char *buffer = kmalloc(SIZE);
 408
 409        if (buffer == NULL)
 410                return -ENOMEM;
 411
 412        if (condition1) {
 413                while (loop1) {
 414                        ...
 415                }
 416                result = 1;
 417                goto out;
 418        }
 419        ...
 420out:
 421        kfree(buffer);
 422        return result;
 423}
 424 
 *
 *
 *
 */
static struct User_Context* Create_User_Context(ulong_t size)
{
	struct User_Context *retUser_Context;	

	/* memory for the structure */
	retUser_Context = (struct User_Context*)Malloc(sizeof(struct User_Context));
 	if(retUser_Context == NULL) 
		goto memfail;
	
	memset((char *) retUser_Context, '\0', sizeof(struct User_Context));
    /* The memory space used by the process. */
    retUser_Context->memory = Malloc(size);
	if(retUser_Context->memory == NULL)
		goto memfail;
	
	memset((char *) retUser_Context->memory, '\0', size);
   
	retUser_Context->size = size;
	return retUser_Context;	

memfail:
	if(retUser_Context->memory == NULL)
		Free(retUser_Context->memory);

	if(retUser_Context == NULL) 
		Free(retUser_Context);
	return 0;
}

static bool Validate_User_Memory(struct User_Context* userContext,
    ulong_t userAddr, ulong_t bufSize)
{
    ulong_t avail;

    if (userAddr >= userContext->size)
        return false;

    avail = userContext->size - userAddr;
    if (bufSize > avail)
        return false;

    return true;
}

/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

/*
 * Destroy a User_Context object, including all memory
 * and other resources allocated within it.
 */
void Destroy_User_Context(struct User_Context* userContext)
{

	Free(userContext->memory);
   	Free_Segment_Descriptor(userContext->ldtDescriptor);
   	Free(userContext);
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
     * ok - Determine where in memory each executable segment will be placed
     * ok - Determine size of argument block and where it memory it will
     *   be placed
     * - Copy each executable segment into memory
     * - Format argument block in memory
     * - In the created User_Context object, set code entry point
     *   address, argument block address, and initial kernel stack pointer
     *   address
     */
	int i;
	ulong_t size; 
	ulong_t maxva = 0;
	unsigned numArgs;
	ulong_t argBlockSize;

	/* calculates arg block length */	
	Get_Argument_Block_Size(command, &numArgs, &argBlockSize);

	/* Find maximum virtual address */
	for (i = 0; i < exeFormat->numSegments; ++i) {
		struct Exe_Segment *segment = &exeFormat->segmentList[i];
		ulong_t topva = segment->startAddress + segment->sizeInMemory; 

		if (topva > maxva)
			maxva = topva;
	}
	/* calculates memory size for the program, the stack and the args */
	size = Round_Up_To_Page(maxva) + Round_Up_To_Page(DEFAULT_USER_STACK_SIZE + argBlockSize); 
	
	/* creates the user context */
	if( (*pUserContext = Create_User_Context(size)) == 0 )
		return -1;

	/* Load segment data into memory */
	for (i = 0; i < exeFormat->numSegments; ++i) {
		struct Exe_Segment *segment = &exeFormat->segmentList[i];
		memcpy((*pUserContext)->memory + segment->startAddress,
				exeFileData + segment->offsetInFile,
				segment->lengthInFile);
	}
	
	/* copy the args just after the data segment */
	struct Exe_Segment *segment = &exeFormat->segmentList[1];
	int userAddress = segment->startAddress + segment->lengthInFile;
	Print("%.8x\n", userAddress);
	Format_Argument_Block( (*pUserContext)->memory + userAddress, numArgs, userAddress, command);
	
	/* to show some part of the code */
//	memDump((*pUserContext)->memory + userAddress, 0x6c + 0x20);

	/* allocate the LDT descriptor in the GDT */
	(*pUserContext)->ldtDescriptor = Allocate_Segment_Descriptor();
	Init_LDT_Descriptor(
			(*pUserContext)->ldtDescriptor,
			(*pUserContext)->ldt,
			NUM_USER_LDT_ENTRIES);

	/* the LDT selector  */
	(*pUserContext)->ldtSelector = Selector( 
			KERNEL_PRIVILEGE, 
			true,	/* in the GDT */ 
			Get_Descriptor_Index( (*pUserContext)->ldtDescriptor ));

	/* code descriptor */
	Init_Code_Segment_Descriptor(
			&(*pUserContext)->ldt[0],
			(ulong_t)(*pUserContext)->memory , 			// base address
			((*pUserContext)->size/PAGE_SIZE)+10,	 	// FIXME is ok??? num pages
			USER_PRIVILEGE	
			);

	(*pUserContext)->csSelector = Selector( 
			USER_PRIVILEGE, 
			false,										/* LDT */ 
			0 );										/* descriptor index */

	/* data descriptor */
	Init_Data_Segment_Descriptor(
			&(*pUserContext)->ldt[1],
			(ulong_t)(*pUserContext)->memory , 			// base address
			((*pUserContext)->size/PAGE_SIZE)+10,	 	// FIXME is ok??? num pages
			USER_PRIVILEGE	
			);

	(*pUserContext)->dsSelector = Selector( 
			USER_PRIVILEGE, 
			false,										/* LDT */ 
			1 );										/* descriptor index */

	/* entry point */	
	(*pUserContext)->entryAddr = exeFormat->entryAddr;

	/* Address of argument block in user memory */
    (*pUserContext)->argBlockAddr = userAddress;

	(*pUserContext)->stackPointerAddr = (*pUserContext)->size;
    /* Initial stack pointer */
    // TODO stackPointerAddr;

    /*
     * May use this in future to allow multiple threads
     * in the same user context
	 *	
	 *	int refCount;
	 *
     */

	/* for debuging */
	Print("virt Space: From %p to %p\n", (*pUserContext)->memory, (*pUserContext)->memory + (*pUserContext)->size);
	Print("%.8lX\n", (*pUserContext)->size);
	//Print("codeSelector=%08x,DataSelector=%08x\n", (*pUserContext)->csSelector,
	//		(*pUserContext)->dsSelector);

//j	memDump((*pUserContext)->memory+0x24f0,0x100);
//	memDump((void*)(*pUserContext),0x100);
	return 0;
}

/*
 * Copy data from user memory into a kernel buffer.
 * Params:
 * destInKernel - address of kernel buffer
 * srcInUser - address of user buffer
 * bufSize - number of bytes to copy
 *
 * Returns:
 *   true if successful, false if user buffer is invalid (i.e.,
 *   doesn't correspond to memory the process has a right to
 *   access)
 */
bool Copy_From_User(void* destInKernel, ulong_t srcInUser, ulong_t bufSize)
{
    /*
     * Hints:
     * TODO
	 *
	 * - the User_Context of the current process can be found
     *   from g_currentThread->userContext
     * - the user address is an index relative to the chunk
     *   of memory you allocated for it
     * - make sure the user buffer lies entirely in memory belonging
     *   to the process
     */
	if(Validate_User_Memory(g_currentThread->userContext, srcInUser, bufSize)){
		memcpy(destInKernel, (char*)(g_currentThread->userContext)->memory + srcInUser, bufSize );
		return true;
	}else{
		return false;
	}

}

/*
 * Copy data from kernel memory into a user buffer.
 * Params:
 * destInUser - address of user buffer
 * srcInKernel - address of kernel buffer
 * bufSize - number of bytes to copy
 *
 * Returns:
 *   true if successful, false if user buffer is invalid (i.e.,
 *   doesn't correspond to memory the process has a right to
 *   access)
 */
bool Copy_To_User(ulong_t destInUser, void* srcInKernel, ulong_t bufSize)
{
	if(Validate_User_Memory(g_currentThread->userContext, destInUser, bufSize)){
		memcpy( (char*)(g_currentThread->userContext)->memory + destInUser, srcInKernel, bufSize );
		return true;
	}else{
		return false;
	}
}

/*
 * Switch to user address space belonging to given
 * User_Context object.
 * Params:
 * userContext - the User_Context
 */
void Switch_To_Address_Space(struct User_Context *userContext)
{
    /* Load the task register */
    __asm__ __volatile__ (
	"lldt %0"
	:
	: "a" (userContext->ldtSelector)
    );
    /*
     * Hint: you will need to use the lldt assembly language instruction
     * to load the process's LDT by specifying its LDT selector.
     */
}

