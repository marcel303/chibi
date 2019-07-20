#pragma once

#include <string>

namespace chibi
{
	static const int STRING_BUFFER_SIZE = (1 << 12);

	struct StringBuilder
	{
		std::string text;
		
		StringBuilder()
		{
			text.reserve(1 << 16);
		}
		
		void Append(const char c)
		{
			text.push_back(c);
		}
		
		void Append(const char * text)
		{
			this->text.append(text);
		}
		
		void AppendFormat(const char * format, ...);
	};
}
