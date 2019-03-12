#pragma once

#include <stdio.h>
#include <string>
#include <vector>

struct FileHandle
{
	FILE * f = nullptr;
	
	FileHandle(const char * filename, const char * mode)
	{
		f = fopen(filename, mode);
	}
	
	~FileHandle()
	{
		if (f != nullptr)
		{
			//printf("warning: file handle not closed normally. closing it now!\n");
			
			close();
		}
	}
	
	void close()
	{
		fclose(f);
		f = nullptr;
	}
	
	operator FILE*()
	{
		return f;
	}
};

std::vector<std::string> listFiles(const char * path, bool recurse);

bool write_if_different(const char * text, const char * filename);
