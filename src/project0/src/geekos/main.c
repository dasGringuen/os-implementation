/*
 * GeekOS C code entry point
 * Copyright (c) 2001,2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2004, Iulian Neamtiu <neamtiu@cs.umd.edu>
 * $Revision: 1.51 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/bootinfo.h>
#include <geekos/string.h>
#include <geekos/screen.h>
#include <geekos/mem.h>
#include <geekos/crc32.h>
#include <geekos/tss.h>
#include <geekos/int.h>
#include <geekos/kthread.h>
#include <geekos/trap.h>
#include <geekos/timer.h>
#include <geekos/keyboard.h>

/*
 *	First Thread
 */
void myFunc(){
	Keycode gotKey = 0;

	Print("Hello from Adrian !\n");

	/* wait until gets a Cr-D */
	do{
		/*
			Print("0%.4x\n",gotKey);
		*/

		if((gotKey & KEY_RELEASE_FLAG)){
			Print("%c",gotKey);
		}
		/* get the char here to avoid printing 'd' at the end */
		gotKey = Wait_For_Key();	
			Print("wait");

	}while(	!((gotKey & KEY_CTRL_FLAG) 	&&	
	  	 	(gotKey & KEY_RELEASE_FLAG)	&&
			((char)gotKey == 'd')));

	Print("\nBye bye my thread,,, snifff!\n");
}

void secondThread(){
	int i;
	while(0){
		for(i=0; i<10000;i++);
	//	Print(".");
	}
}


/*
 * Kernel C code entry point.
 * Initializes kernel subsystems, mounts filesystems,
 * and spawns init process.
 */
void Main(struct Boot_Info* bootInfo)
{
    Init_BSS();
    Init_Screen();
    Init_Mem(bootInfo);
    Init_CRC32();
    Init_TSS();
    Init_Interrupts();
    Init_Scheduler();
    Init_Traps();
    Init_Timer();
    Init_Keyboard();

    Set_Current_Attr(ATTRIB(BLACK, GREEN|BRIGHT));
    Print("Welcome to GeekOS!\n");
    Set_Current_Attr(ATTRIB(BLACK, GRAY));

	/* kthread test */
	Start_Kernel_Thread(
			&myFunc,
			0,					/* arguments for the thread function */
			PRIORITY_NORMAL,
			0					/*detached - use false for kernel threads*/
			);

	/* kthread test */
	Start_Kernel_Thread(
			&secondThread,
			0,					/* arguments for the thread function */
			PRIORITY_NORMAL,
			0					/*detached - use false for kernel threads*/
			);


    /* Now this thread is done. */
    Exit(0);
}









