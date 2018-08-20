#include "repo1.h"
#include <stdio.h>

void testRepo1()
{
	printf("this is repo1!\n");

	printf("\twe are compiled with OPTION_A set to %d\n", OPTION_A);
	printf("\twe are compiled with OPTION_B set to %d\n", OPTION_B);

#if defined(OPTION_C)
	printf("OPTION_C is defined\n");
#endif
}
