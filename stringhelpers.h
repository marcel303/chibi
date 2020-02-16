#pragma once

#include <ctype.h> // isspace, tolower
#include <string>
#include <string.h>

namespace chibi
{
	static bool is_whitespace(const char c)
	{
		return isspace(c);
	}

	static bool is_comment_or_whitespace(const char * line)
	{
		for (int i = 0; line[i] != 0; ++i)
		{
			if (line[i] == '#')
				return true;
			
			if (is_whitespace(line[i]) == false)
				return false;
		}
		
		return true;
	}

	// eats an arbitrary word and stores the result in 'word'. has built-in support for quoted strings
	static bool eat_word_v2(char *& line, const char *& word)
	{
		while (*line != 0 && is_whitespace(*line) == true)
			line++;
		
		if (*line == 0)
			return false;
		
		const bool isQuoted = *line == '"';

		if (isQuoted)
		{
			line++;

			word = line;
			
			while (*line != 0 && *line != '"')
				line++;
		}
		else
		{
			word = line;
			
			while (*line != 0 && is_whitespace(*line) == false)
				line++;
		}
		
		if (line > word)
		{
			// null-terminate the extracted text
			
			if (*line != 0)
			{
				*line = 0;
				line++;
			}

			// make sure to increment the line pointer until we reach non-whitespace territory again

			while (is_whitespace(*line))
				line++;
			
			return true;
		}
		else
		{
			return false;
		}
	}

	// checks if 'line' begin with 'word' and eats 'word' from 'line' when it does
	static bool eat_word(char * & line, const char * word)
	{
		while (is_whitespace(*line))
			line++;
		
		int index = 0;
		
		while (word[index] != 0)
		{
			if (line[index] != word[index])
				return false;
			
			index++;
		}

		if (line[index] != 0 && is_whitespace(line[index]) == false)
			return false;

		while (is_whitespace(line[index]))
			index++;

		line += index;
		return true;
	}

	static bool do_concat(char *& dst, int & dstSize, const char * s)
	{
		for (int i = 0; s[i] != 0; ++i)
		{
			if (dstSize == 0)
				return false;
			
			*dst = s[i];
			
			dst += 1;
			dstSize -= 1;
		}
		
		if (dstSize == 0)
			return false;
		
		*dst = 0;
		
		return true;
	}

	static bool concat(char * dst, int dstSize, const char * s1, const char * s2 = nullptr, const char * s3 = nullptr, const char * s4 = nullptr)
	{
		return
			do_concat(dst, dstSize, s1) &&
			(s2 == nullptr || do_concat(dst, dstSize, s2)) &&
			(s3 == nullptr || do_concat(dst, dstSize, s3)) &&
			(s4 == nullptr || do_concat(dst, dstSize, s4));
	}

	static bool copy_string(char * dst, int dstSize, const char * s)
	{
		return concat(dst, dstSize, s);
	}

	static bool get_path_from_filename(const char * filename, char * path, int pathSize)
	{
		if (!concat(path, pathSize, filename))
			return false;

		char * term = nullptr;

		for (int i = 0; path[i] != 0; ++i)
			if (path[i] == '/')
				term = &path[i];

		if (term == nullptr)
			return false;

		*term = 0;
		
		return true;
	}

	static std::string get_path_extension(const std::string & path, const bool to_lower)
	{
		size_t pos = path.find_last_of('.');
		
		if (pos == std::string::npos)
			return std::string();
		else
			pos++;
		
		std::string extension = path.substr(pos);
		
		if (to_lower)
		{
			for (auto & c : extension)
				c = tolower(c);
		}
		
		return extension;
	}

	static bool string_starts_with(const std::string & text, const std::string & substring)
	{
		const size_t length1 = text.length();
		const size_t length2 = substring.length();

		if (length1 < length2)
			return false;

		for (size_t i = 0; i < length2; ++i)
			if (text[i] != substring[i])
				return false;

		return true;
	}

	static bool string_ends_with(const std::string & text, const std::string & substring)
	{
		const size_t length1 = text.length();
		const size_t length2 = substring.length();

		if (length1 < length2)
			return false;

		for (size_t i = length1 - length2, j = 0; i < length1; ++i, ++j)
			if (text[i] != substring[j])
				return false;

		return true;
	}

	static bool match_wildcard(const char * in_text, const char * wildcard)
	{
		const char * text = in_text;
		
		while (wildcard[0] != 0)
		{
			if (wildcard[0] == ';')
			{
				text = in_text;
				wildcard++;
			}
			else if (wildcard[0] == '*')
			{
				if (wildcard[1] == 0 || wildcard[1] == ';')
					return true;
				else
				{
					while (text[0] != 0 && text[0] != wildcard[1])
						text++;
					
					if (text[0] == 0)
						return false;
					
					wildcard++;
				}
			}
			else
			{
				if (text[0] != wildcard[0])
				{
					while (wildcard[0] != 0 && wildcard[0] != ';')
						wildcard++;
				}
				else
				{
					text++;
					wildcard++;
				}
			}
		}
		
		return text[0] == 0;
	}
}
