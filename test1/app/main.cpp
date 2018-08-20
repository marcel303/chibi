#include <stdio.h>
#include "repo1.h"
#include "repo2.h"
#include "system.h"

int main(int argc, const char * argv[])
{
	printf("hello world\n");
	
	testRepo1();
	testRepo2();
	
	printf("system name: %s\n", SYSTEM_NAME);
	
	return 0;
}
