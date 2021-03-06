/*
 * Common user mode functions
 * Copyright (c) 2001,2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.50 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/errno.h>
#include <geekos/ktypes.h>
#include <geekos/kassert.h>
#include <geekos/int.h>
#include <geekos/mem.h>
#include <geekos/malloc.h>
#include <geekos/kthread.h>
#include <geekos/vfs.h>
#include <geekos/tss.h>
#include <geekos/user.h>

/*
 * This module contains common functions for implementation of user
 * mode processes.
 */

/*
 * Associate the given user context with a kernel thread.
 * This makes the thread a user process.
 */
void Attach_User_Context(struct Kernel_Thread* kthread, struct User_Context* context)
{
    KASSERT(context != 0);
    kthread->userContext = context;

    Disable_Interrupts();

    /*
     * We don't actually allow multiple threads
     * to share a user context (yet)
     */
    KASSERT(context->refCount == 0);

    ++context->refCount;
    Enable_Interrupts();
}

/*
 * If the given thread has a user context, detach it
 * and destroy it.  This is called when a thread is
 * being destroyed.
 */
void Detach_User_Context(struct Kernel_Thread* kthread)
{
    struct User_Context* old = kthread->userContext;

    kthread->userContext = 0;

    if (old != 0) {
	int refCount;

	Disable_Interrupts();
        --old->refCount;
	refCount = old->refCount;
	Enable_Interrupts();

	/*Print("User context refcount == %d\n", refCount);*/
        if (refCount == 0)
            Destroy_User_Context(old);
    }
}

/*
 * Spawn a user process.
 * Params:
 *   program - the full path of the program executable file
 *   command - the command, including name of program and arguments
 *   pThread - reference to Kernel_Thread pointer where a pointer to
 *     the newly created user mode thread (process) should be
 *     stored
 * Returns:
 *   The process id (pid) of the new process, or an error code
 *   if the process couldn't be created.  Note that this function
 *   should return ENOTFOUND if the reason for failure is that
 *   the executable file doesn't exist.
 */
int Spawn(const char *program, const char *command, struct Kernel_Thread **pThread)
{
    /*
     * Hints:
     * - Call Read_Fully() to load the entire executable into a memory buffer
     * - Call Parse_ELF_Executable() to verify that the executable is
     *   valid, and to populate an Exe_Format data structure describing
     *   how the executable should be loaded
     * - Call Load_User_Program() to create a User_Context with the loaded
     *   program
     * - Call Start_User_Thread() with the new User_Context
     *
     * If all goes well, store the pointer to the new thread in
     * pThread and return 0.  Otherwise, return an error code.
     */
	char *exeFileData = 0;
	ulong_t exeFileLength;
	struct Exe_Format exeFormat;
	struct User_Context *pUserContext;
	int retVal = 0;
	static int temp = 0;

  /*
   * Load the executable file data, parse ELF headers,
   * and load code and data segments into user memory.
   */

	Print("Reading %s...\n", program);

	memDump(program,0x10);
	memDump(command,0x10);

	if(temp++ == 1){
		Print("algo\n");
	//	goto fail;
	}

	if (Read_Fully(program, (void**) &exeFileData, &exeFileLength) != 0)
	{
		retVal = ENOTFOUND;
		Print("Read_Fully failed to read %s from disk\n", program);
		goto fail;
	}


	if (Parse_ELF_Executable(exeFileData, exeFileLength, &exeFormat) != 0)
	{
		retVal = ENOEXEC;
		Print("Parse_ELF_Executable failed\n");
		goto fail;
	}


	if(Load_User_Program(exeFileData, 
				exeFileLength,
				&exeFormat, 
				command,
				&pUserContext) != 0)
	{
		retVal = EUNSPECIFIED;
		Print("Load_User_Program failed\n");
		goto fail;
	}

//	Print("codeSelector=%08x,DataSelector=%08x\n", pUserContext->csSelector,
//			pUserContext->dsSelector);

	*pThread = Start_User_Thread(pUserContext, false);
    /*
     * User program has been loaded, so we can free the
     * executable file data now.
     */

fail:
    if(exeFileData)
		Free(exeFileData);
    exeFileData = 0;
	return retVal; 
}

/*
 * If the given thread has a User_Context,
 * switch to its memory space.
 *
 * Params:
 *   kthread - the thread that is about to execute
 *   state - saved processor registers describing the state when
 *      the thread was interrupted
 */
static int nada = 0;

void Switch_To_User_Context(struct Kernel_Thread* kthread, struct Interrupt_State* state)
{
	if(kthread->userContext != NULL){
	//	Print("%d",nada);
		if(nada == 0){
			Dump_stack_register();
			Print("%lx\n",(ulong_t)kthread->stackPage);
}
		Switch_To_Address_Space(kthread->userContext );
		Set_Kernel_Stack_Pointer(((ulong_t) kthread->stackPage) + PAGE_SIZE);
		if(nada++ == 0)
			Dump_stack_register();

	}
}

