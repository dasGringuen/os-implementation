/*
 * System call handlers
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003,2004 David Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.59 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/syscall.h>
#include <geekos/errno.h>
#include <geekos/kthread.h>
#include <geekos/int.h>
#include <geekos/elf.h>
#include <geekos/malloc.h>
#include <geekos/screen.h>
#include <geekos/keyboard.h>
#include <geekos/string.h>
#include <geekos/user.h>
#include <geekos/timer.h>
#include <geekos/vfs.h>

/*
 * Null system call.
 * Does nothing except immediately return control back
 * to the interrupted user program.
 * Params:
 *  state - processor registers from user mode
 *
 * Returns:
 *   always returns the value 0 (zero)
 */
static int Sys_Null(struct Interrupt_State* state)
{
	Print("Null system call\n");
    return 0;
}

/*
 * Exit system call.
 * The interrupted user process is terminated.
 * Params:
 *   state->ebx - process exit code
 * Returns:
 *   Never returns to user mode!
 */
static int Sys_Exit(struct Interrupt_State* state)
{
	Enable_Interrupts();
    Detach_User_Context(g_currentThread);
    Disable_Interrupts();
    Exit(state->ebx);
    return 0;
}

/*
 * Print a string to the console.
 * Params:
 *   state->ebx - user pointer of string to be printed
 *   state->ecx - number of characters to print
 * Returns: 0 if successful, -1 if not
 */
static int Sys_PrintString(struct Interrupt_State* state)
{
	//Print("Printing %d bytes, 0X%.8X\n", state->ecx, state->ebx);

	char *kMem = (char*)Malloc(state->ecx + 1);	
	memset(kMem, '\0', state->ecx + 1);
		
	if(Copy_From_User(kMem, state->ebx, state->ecx))
		Put_Buf(kMem, state->ecx );
   
	Free(kMem);
	return 0;
}

/*
 * Get a single key press from the console.
 * Suspends the user process until a key press is available.
 * Params:
 *   state - processor registers from user mode
 * Returns: the key code
 */
static int Sys_GetKey(struct Interrupt_State* state)
{
	Keycode gotKey = 0;

	gotKey = Wait_For_Key();	
	return gotKey;
}

/*
 * Set the current text attributes.
 * Params:
 *   state->ebx - character attributes to use
 * Returns: always returns 0
 */
static int Sys_SetAttr(struct Interrupt_State* state)
{

	Set_Current_Attr(state->ebx);
    return 0;
}

/*
 * Get the current cursor position.
 * Params:
 *   state->ebx - pointer to user int where row value should be stored
 *   state->ecx - pointer to user int where column value should be stored
 * Returns: 0 if successful, -1 otherwise
 */
static int Sys_GetCursor(struct Interrupt_State* state)
{
    int y=0, x=0;

    Get_Cursor(&y, &x);

    if (! Copy_To_User(state->ebx, &y, sizeof(int))) {
        return -1;
    }
    if (! Copy_To_User(state->ecx, &x, sizeof(int))) {
        return -1;
    }

    return 0;
}

/*
 * Set the current cursor position.
 * Params:
 *   state->ebx - new row value
 *   state->ecx - new column value
 * Returns: 0 if successful, -1 otherwise
 */
static int Sys_PutCursor(struct Interrupt_State* state)
{
	if (! Put_Cursor( (int) state->ebx, (int) state->ecx) )
   		return -1;
  	else
		return 0;
}

/*
 * Create a new user process.
 * Params:
 *   state->ebx - user address of name of executable
 *   state->ecx - length of executable name
 *   state->edx - user address of command string
 *   state->esi - length of command string
 * Returns: pid of process if successful, error code (< 0) otherwise
 */
static int Sys_Spawn(struct Interrupt_State* state)
{
    int retVal;
    char *exeName = 0;
    char *command = 0;
	ulong_t exeNameLen = state->ecx + 1; 	/* +1 to add the 0 NULL later */
	ulong_t commandLen = state->esi + 1; 		/* +1 to add the 0 NULL later */
	struct Kernel_Thread* kthread;	

	/* get some memory for the exe name and the args */
    exeName = (char*) Malloc(exeNameLen);
    if (exeName == 0)
		goto memfail;

    command = (char*) Malloc(commandLen);
    if (command == 0)
		goto memfail;

	memset(exeName, '\0', exeNameLen);
	if(!Copy_From_User(exeName, state->ebx, exeNameLen)){
		retVal = EUNSPECIFIED;
		Print("Couldn't copy the Exe name from user space\n");
		goto fail;
	}

	memset(command, '\0', commandLen);
	if(!Copy_From_User(command, state->edx, commandLen)){
		retVal = EUNSPECIFIED;
		Print("Couldn't copy from user space\n");
		goto fail;
	}

	Enable_Interrupts();
	if(Spawn(exeName, command, &kthread)){
		retVal = EUNSPECIFIED;
		Print("Error while spawning\n");
		goto fail;
	}
	Disable_Interrupts();	

	if(exeName)
		Free(exeName);
	
	if (command) 
		Free(command);
   	
//	Print("Father Thread address:%8X, Id:%d\n",(int)g_currentThread, kthread->pid);
	return kthread->pid;

memfail:
    retVal = ENOMEM;

fail:
	if(exeName)
		Free(exeName);
	
	if (command) 
		Free(command);
	return retVal;
}

/*
 * Wait for a process to exit.
 * Params:
 *   state->ebx - pid of process to wait for
 * Returns: the exit code of the process,
 *   or error code (< 0) on error
 */
static int Sys_Wait(struct Interrupt_State* state)
{
	int pid = state->ebx;
	struct Kernel_Thread* childThread = 0;  

	Print("Waiting for Thread ID %d\n", pid);

	if((childThread = Lookup_Thread(pid)) ){
		Enable_Interrupts();
		int exitCode = Join(childThread);
		Disable_Interrupts();
		return exitCode;
	}else{
		Print("Error there is no child thread with the ID %d\n", pid);
		return -1;
	}
}

/*
 * Get pid (process id) of current thread.
 * Params:
 *   state - processor registers from user mode
 * Returns: the pid of the current thread
 */
static int Sys_GetPID(struct Interrupt_State* state)
{
	return g_currentThread->pid;
}

/*
 * Global table of system call handler functions.
 */
const Syscall g_syscallTable[] = {
    Sys_Null,
    Sys_Exit,
    Sys_PrintString,
    Sys_GetKey,
    Sys_SetAttr,
    Sys_GetCursor,
    Sys_PutCursor,
    Sys_Spawn,
    Sys_Wait,
    Sys_GetPID,
};

/*
 * Number of system calls implemented.
 */
const int g_numSyscalls = sizeof(g_syscallTable) / sizeof(Syscall);
