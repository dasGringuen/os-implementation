/*
 * A test program for GeekOS user mode
 */

#include <conio.h>
#include <geekos/syscall.h>

char temp[] = "algo";

int main(int argc, char **argv)
{
    int a = 0;


    Print_String( temp);
    Print("%s", temp);
    Print_String( temp);

    a++;
#if 0
    int badsys = -1, rc;
    Print_String("I am the c program\n");

    /* Make an illegal system call */
    __asm__ __volatile__ (
	SYSCALL
	: "=a" (rc)
	: "a" (badsys)
    );
#endif
    return a;
}
