/*
 * ELF executable loading
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003, David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.29 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/errno.h>
#include <geekos/kassert.h>
#include <geekos/ktypes.h>
#include <geekos/screen.h>  /* for debug Print() statements */
#include <geekos/pfat.h>
#include <geekos/malloc.h>
#include <geekos/string.h>
#include <geekos/elf.h>
#include <geekos/kassert.h>

/**
 * From the data of an ELF executable, determine how its segments
 * need to be loaded into memory.
 * @param exeFileData buffer containing the executable file
 * @param exeFileLength length of the executable file in bytes
 * @param exeFormat structure describing the executable's segments
 *   and entry address; to be filled in
 * @return 0 if successful, < 0 on error
 */
int Parse_ELF_Executable(char *exeFileData, ulong_t exeFileLength,
    struct Exe_Format *exeFormat)
{
	int i;

	/* length should not be 0 */	
	if(!exeFileLength)
		return -2;
	
	elfHeader *pElfHeader = (elfHeader*)exeFileData;
	programHeader *pProgHeader = (programHeader*)(exeFileData + pElfHeader->phoff);

	/* should not exceed the max allowed number of segments */
	if(pElfHeader->phnum > EXE_MAX_SEGMENTS)
		return -1;			/* error !!! */

	/* Number of segments in the executable */
	exeFormat->numSegments = pElfHeader->phnum;	

	/* get the data of each segment, in these case 3 .text .data and the stack */
	for (i = 0; i < pElfHeader->phnum; i++) {
		/* Offset of segment in executable file */
		exeFormat->segmentList[i].offsetInFile = pProgHeader->offset;
		/* Length of segment data in executable file */
		exeFormat->segmentList[i].lengthInFile = pProgHeader->fileSize;	 
		/* Start address of segment in user memory */
    	exeFormat->segmentList[i].startAddress = pProgHeader->vaddr;	 
		/* Size of segment in memory */
    	exeFormat->segmentList[i].sizeInMemory = pProgHeader->memSize;	 
		 /* VM protection flags; combination of VM_READ,VM_WRITE,VM_EXEC */
    	exeFormat->segmentList[i].protFlags = pProgHeader->flags;	 
		pProgHeader++;
	}

	/* Code entry point address */
	exeFormat->entryAddr = pElfHeader->entry;

	return 0; /* alles klar! */
}

