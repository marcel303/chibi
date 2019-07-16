#include "filesystem.h"
#include <string.h>

#ifndef _MSC_VER
	#include <dirent.h>
#endif

#ifdef _MSC_VER
	#include <direct.h>
	#include <stdint.h>
	#include <Windows.h>
	#ifndef PATH_MAX
		#define PATH_MAX _MAX_PATH
	#endif
	typedef SSIZE_T ssize_t;
	static ssize_t getline(char ** _line, size_t * _line_size, FILE * file)
	{
		char *& line = *_line;
		size_t & line_size = *_line_size;

		if (line == nullptr)
		{
			line_size = 32;
			line = (char*)malloc(line_size);
		}

		for (;;)
		{
			auto pos = ftell(file);

			const char * r = fgets(line, line_size, file);

			if (r == nullptr)
				break;

			const int length = strlen(line);

			if (length == line_size - 1)
			{
				free(line);

				line_size *= 2;
				line = (char*)malloc(line_size);

				fseek(file, pos, SEEK_SET);
			}
			else
				return length;
		}

		return -1;
	}
#else
    #include <stdio.h>
	#include <unistd.h>
#endif

bool concat(char * dst, int dstSize, const char * s1, const char * s2 = nullptr, const char * s3 = nullptr, const char * s4 = nullptr);

namespace chibi_filesystem
{
	std::vector<std::string> listFiles(const char * path, bool recurse)
	{
	#ifdef WIN32
		std::vector<std::string> result;
		WIN32_FIND_DATAA ffd;
		char wildcard[MAX_PATH];
		sprintf_s(wildcard, sizeof(wildcard), "%s\\*", path);
		HANDLE find = FindFirstFileA(wildcard, &ffd);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
			{
				char fullPath[MAX_PATH];
				if (strcmp(path, "."))
					concat(fullPath, sizeof(fullPath), path, "/", ffd.cFileName);
				else
					concat(fullPath, sizeof(fullPath), ffd.cFileName);

				if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					if (recurse && strcmp(ffd.cFileName, ".") && strcmp(ffd.cFileName, ".."))
					{
						std::vector<std::string> subResult = listFiles(fullPath, recurse);
						result.insert(result.end(), subResult.begin(), subResult.end());
					}
				}
				else
				{
					result.push_back(fullPath);
				}
			} while (FindNextFileA(find, &ffd) != 0);

			FindClose(find);
		}
		return result;
	#else
		std::vector<std::string> result;
		
		std::vector<DIR*> dirs;
		{
			DIR * dir = opendir(path);
			if (dir)
				dirs.push_back(dir);
		}
		
		while (!dirs.empty())
		{
			DIR * dir = dirs.back();
			dirs.pop_back();
			
			dirent * ent;
			
			while ((ent = readdir(dir)) != 0)
			{
				char fullPath[PATH_MAX];
				if (strcmp(path, "."))
					concat(fullPath, sizeof(fullPath), path, "/", ent->d_name);
				else
					concat(fullPath, sizeof(fullPath), ent->d_name);
				
				if (ent->d_type == DT_DIR)
				{
					if (recurse && strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
					{
						std::vector<std::string> subResult = listFiles(fullPath, recurse);
						result.insert(result.end(), subResult.begin(), subResult.end());
					}
				}
				else
				{
					result.push_back(fullPath);
				}
			}
			
			closedir(dir);
		}
		return result;
	#endif
	}

	//

	bool write_if_different(const char * text, const char * filename)
	{
		FileHandle existing_file(filename, "rt");
		
		bool is_equal = false;
		
		if (existing_file != nullptr)
		{
			std::string existing_text;
			
			char * line = nullptr;
			size_t line_size = 0;
			
			for (;;)
			{
				const ssize_t r = getline(&line, &line_size, existing_file);
				
				if (r < 0)
					break;
				
				existing_text.append(line);
			}
			
			free(line);
			line = nullptr;
			
			if (text == existing_text)
				is_equal = true;
			
			existing_file.close();
		}
		
		if (is_equal)
		{
			return true;
		}
		else
		{
			FileHandle file(filename, "wt");
			
			if (file == nullptr)
			{
				return false;
			}
			else
			{
				fprintf(file, "%s", text);
				
				file.close();
				
				return true;
			}
		}
	}
}
