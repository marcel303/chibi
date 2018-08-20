#include "repo2.h"
#include <stdio.h>

void testRepo2()
{
	printf("this is repo2!\n");
	
#if defined(OPTION_A) || defined(OPTION_A) || defined(OPTION_A)
	#error OPTION_A, B or C should not be defined for repo2
#endif
}
