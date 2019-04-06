#include "chibi.h"
#include <limits.h>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <string.h>

#ifdef _MSC_VER
	#include <Windows.h>
	#ifndef PATH_MAX
		#define PATH_MAX _MAX_PATH
	#endif
#endif

#if defined(__GNUC__)
	#define sprintf_s(s, ss, f, ...) snprintf(s, ss, f, __VA_ARGS__)
	#define vsprintf_s(s, ss, f, a) vsnprintf(s, ss, f, a)
	#define strcpy_s(d, ds, s) strcpy(d, s)
	#define sscanf_s sscanf
#endif

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
	vsprintf_s(text, sizeof(text), format, ap);
	va_end(ap);
	
	//
	
	printf("error: %s\n", text);
}

static void show_chibi_cli()
{
	printf("usage: chibi <source_path> <destination_path> ..[-target <wildcard>]\n");
	printf("\t<source_path> the path where to begin looking for the chibi root file\n");
	printf("\t<destination_path> the path where to output the generated cmake file\n");
	printf("\t-target sets an optional filter for the <app_name> or <library_name> to limit the scope of the generated cmake file to only the specific target(s). <wildcard> may specify either the complete target name or a wildcard. when used more than once, multiple targets can be set\n");
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
				report_error("missing target name: %s", option);
				return -1;
			}
			
			build_targets.insert(target);
		}
		else
		{
			report_error("unknown command line option: %s", option);
			return -1;
		}
	}
	
	const int numTargets = build_targets.size();
	const char ** targets = (const char**)alloca(numTargets * sizeof(char*));
	
	int index = 0;
	
	for (auto & target : build_targets)
		targets[index++] = target.c_str();
	
	if (chibi_generate(cwd, src_path, dst_path, targets, numTargets) == false)
		return -1;
	
	return 0;
}
