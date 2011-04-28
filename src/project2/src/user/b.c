/*
 * A test program for GeekOS user mode
 */

#include <conio.h>

int main(int argc, char** argv)
{
    int i = 0;

    Print_String("I am the b program\n");
	Print("Number of args:%d\n", argc);
	//Print("Number of args:%d\n", argc);
	//Print("Number of args:%d\n", argc);

    for (i = 0; i < argc; ++i) {
		Print("Arg %d is %s\n", i,argv[i]);
    }
    return 1;
}
