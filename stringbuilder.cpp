#include "stringbuilder.h"
#include <stdarg.h>

#if defined(__GNUC__)
	#define sprintf_s(s, ss, f, ...) snprintf(s, ss, f, __VA_ARGS__)
	#define vsprintf_s(s, ss, f, a) vsnprintf(s, ss, f, a)
	#define strcpy_s(d, ds, s) strcpy(d, s)
	#define sscanf_s sscanf
#endif

namespace chibi
{
	void StringBuilder::AppendFormat(const char * format, ...)
	{
		va_list va;
		va_start(va, format);
		char text[STRING_BUFFER_SIZE];
		vsprintf_s(text, sizeof(text), format, va);
		va_end(va);

		Append(text);
	}
}
