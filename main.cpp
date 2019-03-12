#include "chibi.h"
#include <limits.h>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <string.h>

static bool eat_arg(int & argc, const char **& argv, const char *& arg)
{
	if (argc == 0)
		return false;
	
	arg = *argv;
	
	argc -= 1;
	argv += 1;
	
	return true;
}

static void report_error(const char * format, ...)
{
	char text[1024];
	va_list ap;
	va_start(ap, format);
	vsprintf(text, format, ap);
	va_end(ap);
	
	//
	
	printf("error: %s\n", text);
}

int main(int argc, const char * argv[])
{
	char cwd[PATH_MAX];
	cwd[0] = 0;
	
	const char * src_path = nullptr;
	const char * dst_path = nullptr;
	
	argc -= 1;
	argv += 1;
	
	if (argc == 0)
	{
		show_chibi_cli();
		printf("\n");
		
		show_chibi_syntax();
		return -1;
	}
	else if (!eat_arg(argc, argv, src_path))
	{
		report_error("missing source path");
		return -1;
	}
	else if (!eat_arg(argc, argv, dst_path))
	{
		report_error("missing destination path");
		return -1;
	}
	
	std::set<std::string> build_targets;
	
	while (argc > 0)
	{
		const char * option;
		
		if (!eat_arg(argc, argv, option))
			break;
		
		if (!strcmp(option, "-target"))
		{
			const char * target;
			
			if (!eat_arg(argc, argv, target))
			{
				report_error(nullptr, "missing target name: %s", option);
				return -1;
			}
			
			build_targets.insert(target);
		}
		else
		{
			report_error(nullptr, "unknown command line option: %s", option);
			return -1;
		}
	}
	
	const int numTargets = build_targets.size();
	const char ** targets = (const char**)alloca(numTargets * sizeof(char*));
	
	int index = 0;
	
	for (auto & target : build_targets)
		targets[index++] = target.c_str();
	
	if (chibi_process(cwd, src_path, dst_path, targets, numTargets) == false)
		return -1;
	
	return 0;
}
