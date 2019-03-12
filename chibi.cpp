#include "filesystem.h"

#include <algorithm>
#include <deque>
#include <limits.h>
#include <map>
#include <set>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <vector>

/*
brew install ccache
*/

#define ENABLE_PKGCONFIG 0 // todo : pkgconfig shouldn't be used in chibi.txt files. but it would be nice to define libraries using pkgconfig externally, as a sort of aliases, which can be used in a normalized fashion as a regular library

// todo : add basic wildcard support ("include/*.cpp")
// todo : copy generated dylibs into app bundle rpath on macos
// todo : create library targets which are an alias for an existing system library, such as libusb, libsdl2, etc -> will allow to normalize library names, and to use either the system version or compile from source version interchangable

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
	#include <unistd.h>
#endif

// todo : redesign the embed_framework option

#define STRING_BUFFER_SIZE (1 << 12)

static ssize_t s_current_line_length = 0;

static bool file_exist(const char * path)
{
	FILE * f = fopen(path, "rb");

	if (f == nullptr)
		return false;
	else
	{
		fclose(f);
		f = nullptr;

		return true;
	}
}

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
		if (*line != 0)
		{
			*line = 0;
			line++;
		}
		
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

	if (is_whitespace(line[index]) == false && line[index] != 0)
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

/*public*/ bool concat(char * dst, int dstSize, const char * s1, const char * s2 = nullptr, const char * s3 = nullptr, const char * s4 = nullptr)
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

static std::vector<std::string> s_currentFile;

struct ChibiFileScope
{
	ChibiFileScope(const char * filename)
	{
		s_currentFile.push_back(filename);
	}

	~ChibiFileScope()
	{
		s_currentFile.pop_back();
	}
};

static void report_error(const char * line, const char * format, ...)
{
	char text[1024];
	va_list ap;
	va_start(ap, format);
	vsprintf(text, format, ap);
	va_end(ap);
	
	//
	
	if (line != nullptr)
	{
		printf(">>");
		
		for (int i = 0; i < s_current_line_length; )
		{
			while (is_whitespace(line[i]))
				i++;
			
			if (i < s_current_line_length)
			{
				printf(" %s", line + i);
			
				while (line[i] != 0)
					i++;
			
				i++;
			}
		}
		
		printf("\n");
	}
	
	//
	
	printf("error: %s\n", text);
	if (!s_currentFile.empty())
		printf("in file: %s\n", s_currentFile.back().c_str());
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
	{
		report_error(nullptr, "invalid path: %s", filename);
		return false;
	}

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

static bool is_absolute_path(const char * path)
{
#if MACOS || LINUX
	return path[0] == '/';
#else
	// todo : detect absolute Windows path
	return false;
#endif
}

struct ChibiLibraryFile
{
	std::string filename;
	
	std::string group;
	
	bool compile = true;
};

struct ChibiLibraryDependency
{
	enum Type
	{
		kType_Undefined,
		kType_Generated,
		kType_Local,
		kType_Find,
		kType_Global
	};
	
	std::string name;
	std::string path;
	
	Type type = kType_Undefined;
	
	bool embed_framework = false;
};

struct ChibiPackageDependency
{
	enum Type
	{
		kType_Undefined,
		kType_FindPackage,
	#if ENABLE_PKGCONFIG
		kType_PkgConfig
	#endif
	};
	
	std::string name;
	std::string variable_name;
	
	Type type = kType_Undefined;
};

struct ChibiHeaderPath
{
	std::string path;
	
	bool expose = false;
	
	std::string alias_through_copy;
	std::string alias_through_copy_path;
};

struct ChibiCompileDefinition
{
	std::string name;
	std::string value;
	
	bool expose = false;
	
	std::string toolchain;
	
	std::vector<std::string> configs;
};

struct ChibiLibrary
{
	std::string name;
	std::string path;
	std::string group_name;
	std::string chibi_file;
	
	bool shared = false;
	
	bool isExecutable = false;
	
	std::vector<ChibiLibraryFile> files;
	
	std::vector<ChibiLibraryDependency> library_dependencies;
	
	std::vector<ChibiPackageDependency> package_dependencies;
	
	std::vector<ChibiHeaderPath> header_paths;
	
	std::vector<ChibiCompileDefinition> compile_definitions;
	
	std::string resource_path;
	std::vector<std::string> resource_excludes;

	std::vector<std::string> dist_files;
	
	void dump_info() const
	{
		printf("%s: %s\n", isExecutable ? "app" : "library", name.c_str());
		
		for (auto & file : files)
		{
			printf("\tfile: %s\n", file.filename.c_str());
			if (file.group.empty() == false)
				printf("\t\tgroup: %s\n", file.group.c_str());
		}
		
		for (auto & header_path : header_paths)
		{
			printf("\theader path: %s\n", header_path.path.c_str());
			if (header_path.expose)
				printf("\t\texpose\n");
			if (header_path.alias_through_copy.empty() == false)
				printf("\t\talias_through_copy: %s\n", header_path.alias_through_copy.c_str());
		}
	}
};

struct ChibiInfo
{
	std::set<std::string> build_targets;
	
	std::vector<ChibiLibrary*> libraries;
	
	bool library_exists(const char * name) const
	{
		for (auto & library : libraries)
		{
			if (library->name == name)
				return true;
		}
		
		return false;
	}
	
	ChibiLibrary * find_library(const char * name)
	{
		for (auto & library : libraries)
			if (library->name == name)
				return library;
		
		return nullptr;
	}
	
	bool should_build_target(const char * name) const
	{
		if (build_targets.empty())
			return true;
		else if (build_targets.count(name) != 0)
			return true;
		else
		{
			for (auto & build_target : build_targets)
				if (match_wildcard(name, build_target.c_str()))
					return true;
		}
		
		return false;
	}
	
	void dump_info() const
	{
		for (auto & library : libraries)
		{
			library->dump_info();
		}
	}
};

static ChibiInfo s_chibiInfo;

static ChibiLibrary * s_currentLibrary = nullptr;

static std::string s_platform;
static std::string s_platform_full;

static std::vector<std::string> s_cmake_module_paths;

static std::vector<std::string> s_currentGroup;

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
	
	void AppendFormat(const char * format, ...)
	{
		va_list va;
		va_start(va, format);
		char text[STRING_BUFFER_SIZE];
		vsprintf(text, format, va);
		va_end(va);

		Append(text);
	}
};

static bool is_platform(const char * platform)
{
	if (s_platform == platform)
		return true;
	else if (s_platform_full.empty() == false && s_platform_full == platform)
		return true;
	else
		return false;
}

static bool write_if_different(const char * text, const char * filename)
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
			report_error(nullptr, "failed to write to file");
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

static void show_syntax_elem(const char * format, const char * description)
{
	printf("%s\n\t%s\n", format, description);
}

void show_chibi_syntax()
{
	printf("chibi syntax (chibi-root):\n");
	show_syntax_elem("add <path>", "adds a chibi file to the workspace");
	show_syntax_elem("add_root <path>", "adds a chibi-root file to the workspace");
	show_syntax_elem("push_group <name>", "pushes a group name. libraries and apps will be grouped by this name. push_group must be followed by a matching pop_group");
	show_syntax_elem("pop_group", "restored the group name");

	printf("chibi syntax (global):\n");
	show_syntax_elem("app <app_name>", "adds an app target with the given name");
	show_syntax_elem("cmake_module_path <path>", "adds a cmake module path");
	show_syntax_elem("library <library_name> [shared]", "adds a library target with the given name");
	show_syntax_elem("with_platform <platform_name>", "with_platform may be specified in front of every line. when set, lines are filtered based on whether the current platform name matched the given platform name");
	
	printf("\n");
	printf("chibi syntax (within app or library context):\n");
	show_syntax_elem("add_dist_files <file>..", "adds one or more files to to be bundled with the application, when the build type is set to distribution");
	show_syntax_elem("add_files <file>..", "adds one or more files to compile");
	show_syntax_elem("compile_definition <name> <value> [expose]", "adds a compile definition. when <value> is set to *, the compile definition is merely defined, without a value. when <expose> is set, the compile_definition is visible to all targets that depends on the current target");
	show_syntax_elem("depend_library <library_name> [local | find]", "adds a target dependency. <library_name> may refer to a chibi library target, or to a pre-built library or system library. when [local] is set, the file is interpreted as a pre-built library to be found at the given location, relative to the current chibi file. when [find] is set, the library will be searched for on the system");
	show_syntax_elem("depend_package <package_name>", "depends on a package, to be found using one of cmake's find_package scripts. <package_name> defines the name of the cmake package script");
	show_syntax_elem("exclude_files <file>..", "exclude one or more files added before using add_files or scan_files");
	show_syntax_elem("group <group_name>", "specify the group for files to be added subsequently using add_files or scan_files");
	show_syntax_elem("header_path <path> [expose]", "specify a header search path. when [expose] is set, the search path will be propagated to all dependent targets");
	show_syntax_elem("resource_path <path>", "specify the resource_path. CHIBI_RESOURCE_PATH will be set appropriately to the given path for debug and release builds. for the distribution build type, files located at resource_path will be bundled with the app and CHIBI_RESOURCE_PATH will be set to the relative search path within the bundle");
	show_syntax_elem("scan_files <extension_or_wildcard> [path <path>].. [traverse] [group <group_name>] [conglomerate <conglomerate_file>]", "adds files by scanning the given path or the path of the current chibi file. files will be filtered using the extension or wildcard pattern provided. [path] can be used to specify a specific folder to look inside. [traverse] may be set to recursively look for files down the directory hierarchy. when [group] is specified, files found through the scan operation will be grouped by this name in generated ide project files. when [conglomerate] is set, the files will be concatenated into this files, and the generated file will be added instead. [conglomerate] may be used to speed up compile times by compiling a set of files in one go");
	
}

static bool process_chibi_file(const char * filename)
{
	ChibiFileScope chibi_scope(filename);

	s_currentLibrary = nullptr;
	
	char chibi_path[PATH_MAX];

	if (!get_path_from_filename(filename, chibi_path, PATH_MAX))
	{
		report_error(nullptr, "failed to get path from chibi filename");
		return false;
	}
	
	//
	
	FileHandle f(filename, "rt");

	if (f == nullptr)
	{
		report_error(nullptr, "failed to open %s", filename);
		return false;
	}
	else
	{
		char * line = nullptr;
		size_t lineSize = 0;

		for (;;)
		{
			ssize_t r = getline(&line, &lineSize, f);

			if (r < 0)
				break;
			else
			{
				//printf("%s\n", line);
				
				s_current_line_length = r;
				
				if (is_comment_or_whitespace(line))
					continue;
				
				char * linePtr = line;
				
				if (eat_word(linePtr, "with_platform"))
				{
					const char * platform;
					
					if (!eat_word_v2(linePtr, platform))
					{
						report_error(line, "with_platform without platform");
						return false;
					}
					
					if (is_platform(platform) == false)
						continue;
				}
				
				//
				
				if (eat_word(linePtr, "add"))
				{
					const char * location;
					
					if (!eat_word_v2(linePtr, location))
					{
						report_error(line, "missing location");
						return false;
					}
					else
					{
						char chibi_file[PATH_MAX];
						
						if (!concat(chibi_file, sizeof(chibi_file), chibi_path, "/", location, "/chibi.txt"))
							return false;
						
						const int length = s_current_line_length;
						
						if (!process_chibi_file(chibi_file))
							return false;
						
						s_currentLibrary = nullptr;
						
						s_current_line_length = length;
					}
				}
				else if (eat_word(linePtr, "push_group"))
				{
					const char * name;
					
					if (!eat_word_v2(linePtr, name))
					{
						report_error(line, "missing group name");
						return false;
					}
					
					s_currentGroup.push_back(name);
				}
				else if (eat_word(linePtr, "pop_group"))
				{
					if (s_currentGroup.empty())
					{
						report_error(line, "no group left to pop");
						return false;
					}

					s_currentGroup.pop_back();
				}
				else if (eat_word(linePtr, "add_root"))
				{
					const char * location;
					
					if (!eat_word_v2(linePtr, location))
					{
						report_error(line, "missing location");
						return false;
					}
					else
					{
						char chibi_file[PATH_MAX];
						
						if (!concat(chibi_file, sizeof(chibi_file), chibi_path, "/", location, "/chibi-root.txt"))
							return false;
						
						const int length = s_current_line_length;
						if (!process_chibi_file(chibi_file))
							return false;
						s_current_line_length = length;
					}
				}
				else if (eat_word(linePtr, "library"))
				{
					s_currentLibrary = nullptr;
					
					const char * name;
					bool shared = false;
					
					if (!eat_word_v2(linePtr, name))
					{
						report_error(line, "missing name");
						return false;
					}
					
					for (;;)
					{
						const char * option;
						
						if (eat_word_v2(linePtr, option) == false)
							break;
						
						if (!strcmp(option, "shared"))
							shared = true;
						else
						{
							report_error(line, "unknown option: %s", option);
							return false;
						}
					}
					
					if (s_chibiInfo.library_exists(name))
					{
						report_error(line, "library already exists");
						return false;
					}
					
					ChibiLibrary * library = new ChibiLibrary();
					
					library->name = name;
					library->path = chibi_path;
					library->chibi_file = s_currentFile.back();
					
					if (s_currentGroup.empty() == false)
						library->group_name = s_currentGroup.back();
					
					library->shared = shared;
					
					s_chibiInfo.libraries.push_back(library);
					
					s_currentLibrary = library;
				}
				else if (eat_word(linePtr, "app"))
				{
					s_currentLibrary = nullptr;
					
					const char * name;
					
					if (!eat_word_v2(linePtr, name))
					{
						report_error(line, "missing name");
						return false;
					}
					
					if (s_chibiInfo.library_exists(name))
					{
						report_error(line, "app already exists");
						return false;
					}
					
					ChibiLibrary * library = new ChibiLibrary();
					
					library->name = name;
					library->path = chibi_path;
					library->chibi_file = s_currentFile.back();
					
					if (s_currentGroup.empty() == false)
						library->group_name = s_currentGroup.back();
					
					library->isExecutable = true;
					
					s_chibiInfo.libraries.push_back(library);
					
					s_currentLibrary = library;
				}
				else if (eat_word(linePtr, "cmake_module_path"))
				{
					const char * path;
					
					if (!eat_word_v2(linePtr, path))
					{
						report_error(line, "cmake_module_path without path");
						return false;
					}
					
					char full_path[PATH_MAX];
					if (!concat(full_path, sizeof(full_path), chibi_path, "/", path))
					{
						report_error(line, "failed to create absolute path");
						return false;
					}
					
					s_cmake_module_paths.push_back(full_path);
				}
				else if (eat_word(linePtr, "add_files"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "add_files without a target");
						return false;
					}
					else
					{
						for (;;)
						{
							const char * filename;
							
							if (!eat_word_v2(linePtr, filename))
								break;
							
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", filename))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							ChibiLibraryFile file;
							
							file.filename = full_path;
							
							s_currentLibrary->files.push_back(file);
						}
					}
				}
				else if (eat_word(linePtr, "scan_files"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "scan_files without a target");
						return false;
					}
					else
					{
						const char * extension;
						
						if (!eat_word_v2(linePtr, extension))
						{
							report_error(line, "missing extension");
							return false;
						}
						
						bool traverse = false;
						
						const char * platform = nullptr;
						
						const char * path = nullptr;
						
						std::vector<std::string> excluded_paths;
						
						const char * group = nullptr;
						
						const char * merge_into = nullptr;
						
						const char * conglomerate = nullptr;
						
						for (;;)
						{
							const char * option;
							
							if (eat_word_v2(linePtr, option) == false)
								break;
							
							if (!strcmp(option, "traverse"))
								traverse = true;
							else if (!strcmp(option, "platform"))
							{
								if (!eat_word_v2(linePtr, platform))
									report_error(line, "missing platform name");
							}
							else if (!strcmp(option, "path"))
							{
								if (!eat_word_v2(linePtr, path))
									report_error(line, "missing path");
							}
							else if (!strcmp(option, "exclude_path"))
							{
								const char * excluded_path;
								
								if (!eat_word_v2(linePtr, excluded_path))
								{
									report_error(line, "exclude_path without a path");
									return false;
								}
								
								char full_path[PATH_MAX];
								if (!concat(full_path, sizeof(full_path), chibi_path, "/", excluded_path))
								{
									report_error(line, "failed to create absolute path");
									return false;
								}
								
								excluded_paths.push_back(full_path);
							}
							else if (!strcmp(option, "group"))
							{
								if (!eat_word_v2(linePtr, group))
								{
									report_error(line, "missing group name");
									return false;
								}
							}
							else if (!strcmp(option, "merge_into"))
							{
								if (!eat_word_v2(linePtr, merge_into))
								{
									report_error(line, "missing merge_into name");
									return false;
								}
							}
							else if (!strcmp(option, "conglomerate"))
							{
								if (!eat_word_v2(linePtr, conglomerate))
								{
									report_error(line, "missing conglomerate target");
									return false;
								}
							}
							else
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						// scan files
						
						char search_path[PATH_MAX];
						
						if (path == nullptr)
						{
							if (!concat(search_path, sizeof(search_path), chibi_path))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}
						else
						{
							if (!concat(search_path, sizeof(search_path), chibi_path, "/", path))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}
						
						auto filenames = listFiles(search_path, traverse);
						
						const bool is_wildcard = strchr(extension, '*') != nullptr;
						
						auto end = std::remove_if(filenames.begin(), filenames.end(), [&](const std::string & filename) -> bool
							{
								if (is_wildcard)
								{
									if (match_wildcard(filename.c_str(), extension) == false)
										return true;
								}
								else
								{
									if (get_path_extension(filename, true) != extension)
										return true;
								}
							
								if (platform != nullptr && platform != s_platform)
									return true;
								
								for (auto & excluded_path : excluded_paths)
									if (string_starts_with(filename, excluded_path))
										return true;
								
								return false;
							});
						
						filenames.erase(end, filenames.end());
						
						std::vector<ChibiLibraryFile> library_files;
						
						for (auto & filename : filenames)
						{
							ChibiLibraryFile file;
							
							file.filename = filename;
							
							if (group != nullptr)
								file.group = group;
							
							library_files.push_back(file);
						}
						
						if (merge_into != nullptr)
						{
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", merge_into))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							FileHandle target_file(full_path, "wt");
							
							if (target_file == nullptr)
							{
								report_error(line, "failed to open merge_into target");
								return false;
							}
							else
							{
								for (auto & library_file : library_files)
								{
									library_file.compile = false;
									
									FileHandle source_file(library_file.filename.c_str(), "rt");
									
									if (source_file == nullptr)
									{
										report_error(line, "failed to open file: %s", library_file.filename.c_str());
										return false;
									}
									
									char * source_line = nullptr;
									size_t source_lineSize = 0;
									
									for (;;)
									{
										auto s = getline(&source_line, &source_lineSize, source_file);
										
										if (s < 0)
											break;
										
										fprintf(target_file, "%s", source_line);
									}
									
									free(source_line);
									source_line = nullptr;
									
									source_file.close();
								}
								
								target_file.close();
							}
							
							ChibiLibraryFile file;
							
							file.filename = full_path;
							
							if (group != nullptr)
								file.group = group;
							
							library_files.push_back(file);
						}
						
						if (conglomerate != nullptr)
						{
							StringBuilder sb;
							
							sb.Append("// auto-generated. do not hand-edit\n\n");
								
							for (auto & library_file : library_files)
							{
								library_file.compile = false;
								
								sb.AppendFormat("#include \"%s\"\n", library_file.filename.c_str());
							}
							
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", conglomerate))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							if (!write_if_different(sb.text.c_str(), full_path))
							{
								report_error(line, "failed to create conglomerate target");
								return false;
							}
							
							ChibiLibraryFile file;
							
							file.filename = full_path;
							
							if (group != nullptr)
								file.group = group;
							
							library_files.push_back(file);
						}
						
						s_currentLibrary->files.insert(
							s_currentLibrary->files.end(),
							library_files.begin(),
							library_files.end());
					}
				}
				else if (eat_word(linePtr, "exclude_files"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "exclude_files without a target");
						return false;
					}
					else
					{
						for (;;)
						{
							const char * filename;
							
							if (!eat_word_v2(linePtr, filename))
								break;
							
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", filename))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							for (auto fileItr = s_currentLibrary->files.begin(); fileItr != s_currentLibrary->files.end(); )
							{
								auto & file = *fileItr;
								
								if (file.filename == full_path)
									fileItr = s_currentLibrary->files.erase(fileItr);
								else
									fileItr++;
							}
						}
					}
				}
				else if (eat_word(linePtr, "depend_package"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "depend_package without a target");
						return false;
					}
					else
					{
						const char * name;
						ChibiPackageDependency::Type type = ChibiPackageDependency::kType_FindPackage;
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error(line, "missing name");
							return false;
						}
						
						for (;;)
						{
							const char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
						#if ENABLE_PKGCONFIG
							if (!strcmp(option, "pkgconfig"))
								type = ChibiPackageDependency::kType_PkgConfig;
							else
						#endif
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						ChibiPackageDependency package_dependency;
						package_dependency.name = name;
						package_dependency.variable_name = name;
						package_dependency.type = type;
						
						std::replace(package_dependency.variable_name.begin(), package_dependency.variable_name.end(), '-', '_');
						std::replace(package_dependency.variable_name.begin(), package_dependency.variable_name.end(), '.', '_');
						
						s_currentLibrary->package_dependencies.push_back(package_dependency);
					}
				}
				else if (eat_word(linePtr, "depend_library"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "depend_library without a target");
						return false;
					}
					else
					{
						const char * name;
						
						ChibiLibraryDependency::Type type = ChibiLibraryDependency::kType_Generated;
						
						bool embed_framework = false;
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error(line, "missing name");
							return false;
						}
						
						for (;;)
						{
							const char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
							if (!strcmp(option, "local"))
								type = ChibiLibraryDependency::kType_Local;
							else if (!strcmp(option, "find"))
								type = ChibiLibraryDependency::kType_Find;
							else if (!strcmp(option, "global"))
								type = ChibiLibraryDependency::kType_Global;
							else if (!strcmp(option, "embed_framework"))
								embed_framework = true;
							else
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						char full_path[PATH_MAX];
						full_path[0] = 0;
						
						if (type == ChibiLibraryDependency::kType_Local)
						{
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", name))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}
						
						ChibiLibraryDependency library_dependency;
						
						library_dependency.name = name;
						library_dependency.path = full_path;
						library_dependency.type = type;
						library_dependency.embed_framework = embed_framework;
						
						s_currentLibrary->library_dependencies.push_back(library_dependency);
					}
				}
				else if (eat_word(linePtr, "header_path"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "header_path without a target");
						return false;
					}
					else
					{
						const char * path;
						
						if (!eat_word_v2(linePtr, path))
						{
							report_error(line, "missing path");
							return false;
						}
						
						bool expose = false;
						
						const char * platform = nullptr;
						
						const char * alias_through_copy = nullptr;
						
						for (;;)
						{
							const char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
							if (!strcmp(option, "expose"))
								expose = true;
							else if (!strcmp(option, "platform"))
							{
								if (!eat_word_v2(linePtr, platform))
								{
									report_error(line, "missing platform name");
									return false;
								}
							}
							else if (!strcmp(option, "alias_through_copy"))
							{
								if (!eat_word_v2(linePtr, alias_through_copy))
								{
									report_error(line, "missing alias location");
									return false;
								}
							}
							else
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						if (platform != nullptr && platform != s_platform)
							continue;
						
						char full_path[PATH_MAX];
						
						if (strcmp(path, ".") == 0)
						{
							// the target just wants to include its own base path. use a more simplified full path
							
							if (!concat(full_path, sizeof(full_path), chibi_path))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}
						else
						{
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", path))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}
						
						ChibiHeaderPath header_path;
						header_path.path = full_path;
						header_path.expose = expose;
						if (alias_through_copy != nullptr)
							header_path.alias_through_copy = alias_through_copy;
						
						s_currentLibrary->header_paths.push_back(header_path);
					}
				}
				else if (eat_word(linePtr, "compile_definition"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "compile_definition without a target");
						return false;
					}
					else
					{
						const char * name;
						const char * value;
						std::vector<std::string> configs;
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error(line, "missing name");
							return false;
						}
						
						if (!eat_word_v2(linePtr, value))
						{
							report_error(line, "missing value");
							return false;
						}
						
						if (!strcmp(value, "*"))
							value = "";
						
						bool expose = false;
						
						const char * toolchain = "";
						
						for (;;)
						{
							const char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
							if (!strcmp(option, "expose"))
								expose = true;
							else if (!strcmp(option, "toolchain"))
							{
								if (!eat_word_v2(linePtr, toolchain))
								{
									report_error(line, "missing name");
									return false;
								}
							}
							else if (!strcmp(option, "config"))
							{
								const char * config;
								
								if (!eat_word_v2(linePtr, config))
								{
									report_error(line, "missing config name");
									return false;
								}
								
								configs.push_back(config);
							}
							else
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						ChibiCompileDefinition compile_definition;
						
						compile_definition.name = name;
						compile_definition.value = value;
						compile_definition.expose = expose;
						compile_definition.toolchain = toolchain;
						compile_definition.configs = configs;
						
						s_currentLibrary->compile_definitions.push_back(compile_definition);
					}
				}
				else if (eat_word(linePtr, "resource_path"))
				{
					if (s_currentLibrary == nullptr || s_currentLibrary->isExecutable == false)
					{
						report_error(line, "resource_path without a target");
						return false;
					}
					else
					{
						const char * path;
						std::vector<std::string> excludes;
						
						if (!eat_word_v2(linePtr, path))
						{
							report_error(line, "missing path");
							return false;
						}
						
						char full_path[PATH_MAX];

						if (is_absolute_path(path))
						{
							if (!copy_string(full_path, sizeof(full_path), path))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}
						else
						{
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", path))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}

						for (;;)
						{
							const char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
							if (!strcmp(option, "exclude"))
							{
								const char * exclude;

								if (!eat_word_v2(linePtr, exclude))
								{
									report_error(line, "missing exclude filename or pattern");
									return false;
								}

								excludes.push_back(exclude);
							}
							else
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						s_currentLibrary->resource_path = full_path;
						s_currentLibrary->resource_excludes = excludes;
					}
				}
				else if (eat_word(linePtr, "group"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "group without a target");
						return false;
					}
					else
					{
						const char * name;
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error(line, "missing group name");
							return false;
						}
						
						s_currentLibrary->group_name = name;
					}
				}
				else if (eat_word(linePtr, "add_dist_files"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "add_dist_files without a target");
						return false;
					}
					else
					{
						for (;;)
						{
							const char * file;

							if (!eat_word_v2(linePtr, file))
								break;

							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", file))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}

							s_currentLibrary->dist_files.push_back(full_path);
						}
					}
				}
				else
				{
					report_error(line, "syntax error");
					return false;
				}
				
				// check we processed the entire line
				
				for (int i = 0; linePtr[i] != 0; ++i)
				{
					if (linePtr[i] != 0)
					{
						report_error(line, "unexpected text at end of line");
						return false;
					}
				}
			}
		}

		free(line);
		line = nullptr;
		
		f.close();
		
		return true;
	}
}

static const char * translate_toolchain_to_cmake(const std::string & name)
{
	if (name == "msvc")
		return "MSVC";
	
	return nullptr;
}

struct CMakeWriter
{
	bool handle_library(ChibiLibrary & library, std::set<std::string> & traversed_libraries, std::vector<ChibiLibrary*> & libraries)
	{
	#if 0
		if (library.isExecutable)
			printf("handle_app: %s\n", library.name.c_str());
		else
			printf("handle_lib: %s\n", library.name.c_str());
	#endif
	
		traversed_libraries.insert(library.name);
		
		// recurse library dependencies
		
		for (auto & library_dependency : library.library_dependencies)
		{
			if (traversed_libraries.count(library_dependency.name) != 0)
				continue;
			
			if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
			{
				ChibiLibrary * found_library = s_chibiInfo.find_library(library_dependency.name.c_str());
				
				if (found_library == nullptr)
				{
					report_error(nullptr, "failed to find library dependency: %s for target %s", library_dependency.name.c_str(), library.name.c_str());
					return false;
				}
				else
				{
					if (handle_library(*found_library, traversed_libraries, libraries) == false)
						return false;
				}
			}
			else if (library_dependency.type == ChibiLibraryDependency::kType_Local)
			{
				// nothing to do here
			}
			else if (library_dependency.type == ChibiLibraryDependency::kType_Find)
			{
				// nothing to do here
			}
			else if (library_dependency.type == ChibiLibraryDependency::kType_Global)
			{
				// nothing to do here
			}
			else
			{
				report_error(nullptr, "internal error: unknown library dependency type");
				return false;
			}
		}
		
		libraries.push_back(&library);
		
		return true;
	};
	
	template <typename S>
	static bool write_header_paths(S & sb, const ChibiLibrary & library)
	{
		if (!library.header_paths.empty())
		{
			for (auto & header_path : library.header_paths)
			{
				const char * visibility = header_path.expose
					? "PUBLIC"
					: "PRIVATE";
				
				sb.AppendFormat("target_include_directories(%s %s \"%s\")\n",
					library.name.c_str(),
					visibility,
					header_path.alias_through_copy_path.empty() == false
					? header_path.alias_through_copy_path.c_str()
					: header_path.path.c_str());
			}
			
			sb.Append("\n");
		}
		
		return true;
	}
	
	template <typename S>
	static bool write_compile_definitions(S & sb, const ChibiLibrary & library)
	{
		if (!library.compile_definitions.empty())
		{
			for (auto & compile_definition : library.compile_definitions)
			{
				const char * toolchain = translate_toolchain_to_cmake(compile_definition.toolchain);
				
				if (toolchain != nullptr)
				{
					sb.AppendFormat("if (%s)\n", toolchain);
					sb.Append("\t"); // todo : improved indent support
				}
				
				const char * visibility = compile_definition.expose
					? "PUBLIC"
					: "PRIVATE";
				
				for (size_t config_index = 0; config_index == 0 || config_index < compile_definition.configs.size(); ++config_index)
				{
					char condition_begin[64];
					char condition_end[64];
					
					if (compile_definition.configs.empty())
					{
						condition_begin[0] = 0;
						condition_end[0] = 0;
					}
					else
					{
						if (!concat(condition_begin, sizeof(condition_begin), "$<$<CONFIG:", compile_definition.configs[config_index].c_str(), ">:"))
						{
							report_error(nullptr, "failed to create compile definition condition");
							return false;
						}
						
						strcpy(condition_end, ">");
					}
					
					if (compile_definition.value.empty())
					{
						sb.AppendFormat("target_compile_definitions(%s %s %s%s%s)\n",
							library.name.c_str(),
							visibility,
							condition_begin,
							compile_definition.name.c_str(),
							condition_end);
					}
					else
					{
						sb.AppendFormat("target_compile_definitions(%s %s %s%s=%s%s)\n",
							library.name.c_str(),
							visibility,
							condition_begin,
							compile_definition.name.c_str(),
							compile_definition.value.c_str(),
							condition_end);
					}
					
					if (toolchain != nullptr)
					{
						sb.AppendFormat("endif (%s)\n", toolchain);
					}
				}
			}
			
			sb.Append("\n");
		}
		
		return true;
	}
	
	template <typename S>
	static bool write_library_dependencies(S & sb, const ChibiLibrary & library)
	{
		if (!library.library_dependencies.empty())
		{
			StringBuilder find;
			StringBuilder link;
			
			link.AppendFormat("target_link_libraries(%s", library.name.c_str());
			
			for (auto & library_dependency : library.library_dependencies)
			{
				if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
				{
					link.AppendFormat("\n\tPUBLIC %s",
						library_dependency.name.c_str());
				}
				else if (library_dependency.type == ChibiLibraryDependency::kType_Local)
				{
					link.AppendFormat("\n\tPUBLIC %s",
						library_dependency.path.c_str());
				}
				else if (library_dependency.type == ChibiLibraryDependency::kType_Find)
				{
					char var_name[256];
					
					if (!concat(var_name, sizeof(var_name), library.name.c_str(), "_", library_dependency.name.c_str()))
					{
						report_error(nullptr, "failed to construct variable name for 'find' library dependency");
						return false;
						
					}
					
					find.AppendFormat("find_library(%s %s)\n", var_name, library_dependency.name.c_str());
					
					link.AppendFormat("\n\tPUBLIC ${%s}",
						var_name);
				}
				else if (library_dependency.type == ChibiLibraryDependency::kType_Global)
				{
					link.AppendFormat("\n\tPUBLIC %s",
						library_dependency.name.c_str());
				}
				else
				{
					report_error(nullptr, "internal error: unknown library dependency type");
					return false;
				}
			}
			
			if (!find.text.empty())
			{
				find.Append("\n");
				
				sb.Append(find.text.c_str());
			}
			
			link.Append(")\n");
			link.Append("\n");
			
			sb.Append(link.text.c_str());
		}
		
		return true;
	}

	static const char * get_package_dependency_output_name(const std::string & package_dependency)
	{
		/*
		CMake find_package scripts do not always follow the convention of outputting
		<package_name>_INCLUDE_DIRS and <package_name>_LIBRARIES. sometimes a script
		will capitalize the package name or perhaps something more wild. for now I only
		came across the FindFreetype.cmake script which doesn't stick to convention,
		but since CMake in no way enforces the convention, other exceptions may exist.
		in either case, we normalize known exceptions here since it's bad for automation
		to have to deal with these exceptions..
		*/

		if (package_dependency == "Freetype")
			return "FREETYPE";
		else if (package_dependency == "OpenGL")
			return "OPENGL";
		else
			return package_dependency.c_str();
	}
	
	template <typename S>
	static bool write_package_dependencies(S & sb, const ChibiLibrary & library)
	{
		if (!library.package_dependencies.empty())
		{
			for (auto & package_dependency : library.package_dependencies)
			{
				if (package_dependency.type == ChibiPackageDependency::kType_FindPackage)
				{
					sb.AppendFormat("find_package(%s REQUIRED)\n", package_dependency.name.c_str());
					sb.AppendFormat("if (NOT %s_FOUND)\n", package_dependency.name.c_str());
					sb.AppendFormat("\tmessage(FATAL_ERROR \"%s not found\")\n", package_dependency.name.c_str());
					sb.AppendFormat("endif ()\n");
				}
			}
			
		#if ENABLE_PKGCONFIG
			for (auto & package_dependency : library.package_dependencies)
			{
				if (package_dependency.type == ChibiPackageDependency::kType_PkgConfig)
				{
					sb.AppendFormat("pkg_check_modules(%s REQUIRED %s)\n",
						package_dependency.variable_name.c_str(),
						package_dependency.name.c_str());
				}
			}
		#endif
		
			sb.Append("\n");
			
			for (auto & package_dependency : library.package_dependencies)
			{
				if (package_dependency.type == ChibiPackageDependency::kType_FindPackage)
				{
					sb.AppendFormat("target_include_directories(%s PRIVATE \"${%s_INCLUDE_DIRS}\")\n",
						library.name.c_str(),
						get_package_dependency_output_name(package_dependency.name));
				}
			#if ENABLE_PKGCONFIG
				else if (package_dependency.type == ChibiPackageDependency::kType_PkgConfig)
				{
					sb.AppendFormat("target_include_directories(%s PRIVATE \"${%s_INCLUDE_DIRS}\")\n",
						library.name.c_str(),
						package_dependency.variable_name.c_str());
				}
			#endif
			}
			sb.Append("\n");
			
			for (auto & package_dependency : library.package_dependencies)
			{
				if (package_dependency.type == ChibiPackageDependency::kType_FindPackage)
				{
					sb.AppendFormat("target_link_libraries(%s PRIVATE ${%s_LIBRARIES} ${%s_LIBRARY})\n",
						library.name.c_str(),
						get_package_dependency_output_name(package_dependency.name),
						get_package_dependency_output_name(package_dependency.name));
				}
			#if ENABLE_PKGCONFIG
				else if (package_dependency.type == ChibiPackageDependency::kType_PkgConfig)
				{
					sb.AppendFormat("target_link_libraries(%s PRIVATE ${%s_LIBRARIES})\n",
						library.name.c_str(),
						package_dependency.variable_name.c_str());
				}
			#endif
			}
			sb.Append("\n");
		}
		
		return true;
	}
	
	static bool gather_all_library_dependencies(const ChibiLibrary & library, std::vector<ChibiLibraryDependency> & library_dependencies)
	{
		std::set<std::string> traversed_libraries;
		std::deque<const ChibiLibrary*> stack;
		
		stack.push_back(&library);
		
		traversed_libraries.insert(library.name);
		
		while (stack.empty() == false)
		{
			const ChibiLibrary * library = stack.front();
			
			for (auto & library_dependency : library->library_dependencies)
			{
				if (traversed_libraries.count(library_dependency.name) == 0)
				{
					traversed_libraries.insert(library_dependency.name);
					
					library_dependencies.push_back(library_dependency);
					
					if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
					{
						const ChibiLibrary * library = s_chibiInfo.find_library(library_dependency.name.c_str());
						
						if (library == nullptr)
						{
							report_error(nullptr, "failed to resolve library dependency: %s", library_dependency.name.c_str());
							return false;
						}

						stack.push_back(library);
					}
				}
			}
			
			stack.pop_front();
		}
		
		return true;
	}
	
	static bool write_embedded_app_files(StringBuilder & sb, const ChibiLibrary & app)
	{
		std::vector<ChibiLibraryDependency> library_dependencies;
		
		if (!gather_all_library_dependencies(app, library_dependencies))
			return false;
		
		bool has_embed_dependency = false;
		
		for (auto & library_dependency : library_dependencies)
		{
			if (library_dependency.embed_framework)
			{
				has_embed_dependency = true;
				
				// create a custom command where the embedded file(s) are copied into a place where the executable can find it
				
				const char * filename;
				
				auto i = library_dependency.path.find_last_of('/');
				
				if (i == std::string::npos)
					filename = library_dependency.path.c_str();
				else
					filename = &library_dependency.path[i + 1];
				
				if (s_platform == "macos")
				{
					sb.AppendFormat("set(BUNDLE_PATH \"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/%s.app\")\n", app.name.c_str());
					
					if (string_ends_with(filename, ".framework"))
					{
						// use rsync to recursively copy files if this is a framework
						
						// but first make sure the target directory exists
						
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND ${CMAKE_COMMAND} -E make_directory \"${BUNDLE_PATH}/Contents/Frameworks\"\n" \
								"\tDEPENDS \"%s\")\n",
							app.name.c_str(),
							library_dependency.path.c_str());
						
						// rsync
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND rsync -r \"%s\" \"${BUNDLE_PATH}/Contents/Frameworks\"\n" \
								"\tDEPENDS \"%s\")\n",
							app.name.c_str(),
							library_dependency.path.c_str(),
							library_dependency.path.c_str());
					}
					else
					{
						// just copy the file (if it has changed or doesn't exist)
						
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND ${CMAKE_COMMAND} -E copy_if_different \"%s\" \"${BUNDLE_PATH}/Contents/MacOS/%s\"\n" \
								"\tDEPENDS \"%s\")\n",
							app.name.c_str(),
							library_dependency.path.c_str(),
							filename,
							library_dependency.path.c_str());
					}
				}
				else
				{
					sb.AppendFormat(
						"add_custom_command(\n" \
							"\tTARGET %s POST_BUILD\n" \
							"\tCOMMAND ${CMAKE_COMMAND} -E copy_if_different \"%s\" \"${CMAKE_CURRENT_BINARY_DIR}/%s\"\n" \
							"\tDEPENDS \"%s\")\n",
						app.name.c_str(),
						library_dependency.path.c_str(),
						filename,
						library_dependency.path.c_str());
				}
			}
		}
	
		if (has_embed_dependency)
			sb.Append("\n");
		
		for (auto & library_dependency : library_dependencies)
		{
			if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
			{
				const ChibiLibrary * library = s_chibiInfo.find_library(library_dependency.name.c_str());
				
				for (auto & dist_file : library->dist_files)
				{
					// create a custom command where the embedded file(s) are copied into a place where the executable can find it
					
					const char * filename;
				
					auto i = dist_file.find_last_of('/');
				
					if (i == std::string::npos)
						filename = dist_file.c_str();
					else
						filename = &dist_file[i + 1];
					
					sb.AppendFormat(
						"add_custom_command(\n" \
							"\tTARGET %s POST_BUILD\n" \
							"\tCOMMAND ${CMAKE_COMMAND} -E copy_if_different \"%s\" \"${CMAKE_CURRENT_BINARY_DIR}/%s\"\n" \
							"\tDEPENDS \"%s\")\n",
						app.name.c_str(),
						dist_file.c_str(),
						filename,
						dist_file.c_str());
				}
				
				if (library->shared)
				{
				// todo : skip this step when creating a non-distribution build ?
				// todo : when this step is skipped for non-distribution, perhaps may want to skip copying frameworks too ..
				
					// copy generated shared object files into a place where the executable can find it
					
					if (s_platform == "macos")
					{
						sb.AppendFormat("set(BUNDLE_PATH \"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/%s.app\")\n", app.name.c_str());
						
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND ${CMAKE_COMMAND} -E copy_if_different \"$<TARGET_FILE:%s>\" \"${BUNDLE_PATH}/Contents/MacOS/$<TARGET_FILE_NAME:%s>\"\n" \
								"\tDEPENDS \"$<TARGET_FILE:%s>\")\n",
							app.name.c_str(),
							library_dependency.name.c_str(),
							library_dependency.name.c_str(),
							library_dependency.name.c_str());
					}
					
					// todo : also copy generated (dll) files on Windows (?)
				}
			}
		}
		
		return true;
	}
	
	template <typename S>
	static bool output(FILE * f, S & sb)
	{
		if (fprintf(f, "%s", sb.text.c_str()) < 0)
		{
			report_error(nullptr, "failed to write to disk");
			return false;
		}
		
		return true;
	}
	
	bool write(const char * output_filename)
	{
		// gather the library targets to emit
		
		std::set<std::string> traversed_libraries;
		
		std::vector<ChibiLibrary*> libraries;
		
		for (auto & library : s_chibiInfo.libraries)
		{
			if (traversed_libraries.count(library->name) != 0)
				continue;
			
			if (library->isExecutable && s_chibiInfo.should_build_target(library->name.c_str()))
			{
				if (handle_library(*library, traversed_libraries, libraries) == false)
					return false;
			}
		}
		
		for (auto & library : s_chibiInfo.libraries)
		{
			if (traversed_libraries.count(library->name) != 0)
				continue;
			
			if (s_chibiInfo.should_build_target(library->name.c_str()))
			{
				if (handle_library(*library, traversed_libraries, libraries) == false)
					return false;
			}
		}
		
		// sort files by name
		
		for (auto & library : libraries)
		{
			std::sort(library->files.begin(), library->files.end(),
				[](const ChibiLibraryFile & a, const ChibiLibraryFile & b)
				{
					return a.filename < b.filename;
				});
		}
		
		// write CMake output
		
		FileHandle f(output_filename, "wt");
		
		if (f == nullptr)
		{
			report_error(nullptr, "failed to open output file: %s", output_filename);
			return false;
		}
		else
		{
			char generated_path[PATH_MAX];
			if (!concat(generated_path, sizeof(generated_path), "${CMAKE_CURRENT_BINARY_DIR}/generated"))
			{
				report_error(nullptr, "failed to create abolsute path");
				return false;
			}
			
			{
				StringBuilder sb;
				
				sb.Append("cmake_minimum_required(VERSION 3.6)\n");
				sb.Append("\n");
				sb.Append("set(CMAKE_CXX_STANDARD 11)\n");
				sb.Append("\n");
				
				sb.Append("set_property(GLOBAL PROPERTY USE_FOLDERS ON)\n");
				sb.Append("\n");

				// this translates to -fPIC on linux, which is a requirement to build share libraries
				// on macos this is a default option and always set
				sb.Append("set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n");
				sb.Append("\n");
				
				if (!s_cmake_module_paths.empty())
				{
					for (auto & cmake_module_path : s_cmake_module_paths)
						sb.AppendFormat("list(APPEND CMAKE_MODULE_PATH \"%s\")\n", cmake_module_path.c_str());
					sb.Append("\n");
				}
				
				/*
				note : one of chibi's aims is to work well with modern Git-like workflows. in these
				workflows, switching branches is common. one of the downsides of switching branches
				(when using c++) are the increased build times as branch switches often invalidate
				a lot of object files at once. we include here support for 'ccache' for all chibi
				build targets by default, to make recompiles after switching branches faster. ccache,
				which stand for 'compiler cache' sits between the build system and the compiler. it
				acts as a proxy, which will produce object files from cache when possible, or forwards
				the actual compilation to the compiler when it doesn't have a cached version available
				*/
				sb.Append("find_program(CCACHE_PROGRAM ccache)\n");
				sb.Append("if (CCACHE_PROGRAM)\n");
				sb.Append("\tset_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE \"${CCACHE_PROGRAM}\")\n");
				sb.Append("endif (CCACHE_PROGRAM)\n");
				sb.Append("\n");
				
				// some find_package scripts depend on pkg-config
				sb.Append("if (UNIX)\n"); // todo
				sb.Append("\tfind_package(PkgConfig REQUIRED)\n");
				sb.Append("\tif (NOT PkgConfig_FOUND)\n");
				sb.Append("\t\tmessage(FATAL_ERROR \"PkgConfig not found\")\n");
				sb.Append("\tendif ()\n");
				sb.Append("endif (UNIX)\n");
				sb.Append("\n");
				
				sb.Append("set(CMAKE_MACOSX_RPATH ON)\n");
				sb.Append("\n");

				sb.Append("if ((CMAKE_CXX_COMPILER_ID MATCHES \"MSVC\") AND NOT CMAKE_CL_64)\n");
				sb.Append("\tadd_compile_options(/arch:SSE2)\n");
				sb.Append("\tadd_definitions(-D__SSE2__)\n");
				sb.Append("endif ()\n");
				sb.Append("\n");

				// let CMake generate export definitions for all symbols it finds inside the generated object files, to normalize the behavior
				// across Windows and Linux/OSX; which, Windows being the odd one out, both do this by default
				sb.Append("set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)\n");
				sb.Append("\n");
				
				// normalize the group delimiter to be '/'
				sb.Append("set(SOURCE_GROUP_DELIMITER \"/\")\n");
				sb.Append("\n");
				
				// add a 'Distribution' build type. this build type is used for making
				// 'final' builds of an app. on Macos for instance, it creates a self-
				// contained app bundle, including all of the dylibs, frameworks and
				// resources referenced by the app
				sb.Append("list(APPEND CMAKE_CONFIGURATION_TYPES Distribution)\n");
				sb.Append("set(CMAKE_CXX_FLAGS_DISTRIBUTION \"${CMAKE_CXX_FLAGS_RELEASE} -DCHIBI_BUILD_DISTRIBUTION=1\")\n");
				sb.Append("set(CMAKE_C_FLAGS_DISTRIBUTION \"${CMAKE_C_FLAGS_RELEASE} -DCHIBI_BUILD_DISTRIBUTION=1\")\n");
				sb.Append("set(CMAKE_EXE_LINKER_FLAGS_DISTRIBUTION \"${CMAKE_EXE_LINKER_FLAGS_RELEASE}\")\n");
				sb.Append("set(CMAKE_SHARED_LINKER_FLAGS_DISTRIBUTION \"${CMAKE_SHARED_LINKER_FLAGS_RELEASE}\")\n");
				sb.Append("\n");

			// todo : global compile options should be user-defined
				if (is_platform("linux.raspberry-pi"))
				{
					sb.Append("add_compile_options(-mcpu=cortex-a53)\n");
					sb.Append("add_compile_options(-mfpu=neon-fp-armv8)\n");
					sb.Append("add_compile_options(-mfloat-abi=hard)\n");
					sb.Append("add_compile_options(-funsafe-math-optimizations)\n");
					sb.Append("\n");
				}
				
				if (!output(f, sb))
					return false;
			}
			
		#if 0
			{
				// CMake has a nice ability where it shows you which include directories and compile-time definitions
				// are used for the targets being processed. we don't enable this by default, since it generates a lot
				// of messages, so it must be enabled here
				
				StringBuilder sb;
				
				sb.Append("set(CMAKE_DEBUG_TARGET_PROPERTIES\n");
				sb.Append("\tINCLUDE_DIRECTORIES\n");
				sb.Append("\tCOMPILE_DEFINITIONS\n");
				sb.Append(")\n");
				
				if (!output(f, sb))
					return false;
			}
		#endif
		
			{
				// copy header files aliased through copy
				
				StringBuilder sb;
				
				sb.AppendFormat("# --- aliased header paths ---\n");
				sb.Append("\n");
				
				for (auto & library : libraries)
				{
					for (auto & header_path : library->header_paths)
					{
						if (header_path.alias_through_copy.empty() == false)
						{
							char alias_path[PATH_MAX];
							if (!concat(alias_path, sizeof(alias_path), generated_path, "/",
							 	library->name.c_str()))
							{
								report_error(nullptr, "failed to create absolute path");
								return false;
							}
							
							char copy_path[PATH_MAX];
							if (!concat(copy_path, sizeof(copy_path), alias_path, "/", header_path.alias_through_copy.c_str()))
							{
								report_error(nullptr, "failed to create absolute path");
								return false;
							}
							
							sb.AppendFormat("file(MAKE_DIRECTORY \"%s\")\n", copy_path);
							
							header_path.alias_through_copy_path = alias_path;
							
							sb.AppendFormat("file(COPY \"%s/\" DESTINATION \"%s\")\n",
								header_path.path.c_str(),
								copy_path);
							sb.Append("\n");
						}
					}
				}
				
				if (!output(f, sb))
					return false;
			}
			
			for (auto & library : libraries)
			{
				if (library->isExecutable)
					continue;
				
				StringBuilder sb;
				
				sb.AppendFormat("# --- library %s ---\n", library->name.c_str());
				sb.Append("\n");
				
				bool has_compile_disabled_files = false;
				
				sb.Append("add_library(");
				sb.Append(library->name.c_str());
				
				if (library->shared)
					sb.Append("\n\tSHARED");
				else
					sb.Append("\n\tSTATIC");
				
				for (auto & file : library->files)
				{
					sb.Append("\n\t");
					sb.AppendFormat("\"%s\"", file.filename.c_str());
					
					if (file.compile == false)
						has_compile_disabled_files = true;
				}

				if (true)
				{
					// add chibi file to target

					sb.Append("\n\t");
					sb.AppendFormat("\"%s\"", library->chibi_file.c_str());
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
				if (library->group_name.empty() == false)
				{
					sb.AppendFormat("set_target_properties(%s PROPERTIES FOLDER %s)\n",
						library->name.c_str(),
						library->group_name.c_str());
				}
				
				if (has_compile_disabled_files)
				{
					sb.Append("set_source_files_properties(");
					
					for (auto & file : library->files)
					{
						if (file.compile == false)
						{
							sb.Append("\n\t");
							sb.AppendFormat("\"%s\"", file.filename.c_str());
						}
					}
					
					sb.Append("\n\tPROPERTIES HEADER_FILE_ONLY 1");
					
					sb.Append(")\n");
					sb.Append("\n");
				}
				
				if (!write_header_paths(sb, *library))
					return false;
				
				if (!write_compile_definitions(sb, *library))
					return false;
				
				if (!write_library_dependencies(sb, *library))
					return false;
				
				if (!write_package_dependencies(sb, *library))
					return false;
				
				if (s_platform == "windows")
				{
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY LINK_FLAGS \" /SAFESEH:NO\")\n", library->name.c_str());
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" /wd4244\")\n", library->name.c_str()); // disable 'conversion from type A to B, possible loss of data' warning
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" /wd4018\")\n", library->name.c_str()); // disable 'signed/unsigned mismatch' warning
					
					sb.Append("\n");
				}
				
				if (!output(f, sb))
					return false;
			}
			
			for (auto & app : libraries)
			{
				if (app->isExecutable == false)
					continue;
				
				StringBuilder sb;
				
				sb.AppendFormat("# --- app %s ---\n", app->name.c_str());
				sb.Append("\n");
				sb.Append("add_executable(");
				sb.Append(app->name.c_str());
				sb.Append("\n\tMACOSX_BUNDLE");
				
				for (auto & file : app->files)
				{
					sb.Append("\n\t");
					sb.AppendFormat("\"%s\"", file.filename.c_str());
				}

				if (true)
				{
					// add chibi file to target
					
					sb.Append("\n\t");
					sb.AppendFormat("\"%s\"", app->chibi_file.c_str());
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
				if (app->group_name.empty() == false)
				{
					sb.AppendFormat("set_target_properties(%s PROPERTIES FOLDER %s)\n",
						app->name.c_str(),
						app->group_name.c_str());
				}
				
				if (!app->resource_path.empty())
				{
					if (s_platform == "macos")
					{
						sb.AppendFormat("set(BUNDLE_PATH \"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/%s.app\")\n", app->name.c_str());
						
						// use rsync to copy resources
						
						// but first make sure the target directory exists
						
						// note : we use a conditional to check if we're building a deployment app bundle
						
						const char * conditional = "$<$<NOT:$<CONFIG:Distribution>>:echo>";
						
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND %s ${CMAKE_COMMAND} -E make_directory \"${BUNDLE_PATH}/Contents/Resources\"\n" \
								"\tDEPENDS \"%s\")\n",
							app->name.c_str(),
							conditional,
							app->resource_path.c_str());
						
						std::string exclude_args;

						if (app->resource_excludes.empty() == false)
						{
							for (auto & exclude : app->resource_excludes)
							{
								exclude_args += "--exclude '";
								exclude_args += exclude;
								exclude_args += "' ";
							}
						}

						// rsync
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND %s rsync -r %s \"%s/\" \"${BUNDLE_PATH}/Contents/Resources\"\n" \
								"\tDEPENDS \"%s\")\n",
							app->name.c_str(),
							conditional,
							exclude_args.c_str(),
							app->resource_path.c_str(),
							app->resource_path.c_str());
					
						sb.Append("\n");
						
						sb.AppendFormat("target_compile_definitions(%s PRIVATE $<$<NOT:$<CONFIG:Distribution>>:CHIBI_RESOURCE_PATH=\"%s\">)\n",
							app->name.c_str(),
							app->resource_path.c_str());
						sb.Append("\n");
					}
					else
					{
						sb.AppendFormat("target_compile_definitions(%s PRIVATE CHIBI_RESOURCE_PATH=\"%s\")\n",
							app->name.c_str(),
							app->resource_path.c_str());
						sb.Append("\n");
					}
				}
				
				if (!write_header_paths(sb, *app))
					return false;
				
				if (!write_compile_definitions(sb, *app))
					return false;
				
				if (!write_library_dependencies(sb, *app))
					return false;
				
				if (!write_package_dependencies(sb, *app))
					return false;
				
				if (!write_embedded_app_files(sb, *app))
					return false;
				
			// todo : let libraries and apps add target properties
				if (s_platform == "windows")
				{
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY LINK_FLAGS \" /SAFESEH:NO\")\n", app->name.c_str());
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" /wd4244\")\n", app->name.c_str()); // disable 'conversion from type A to B, possible loss of data' warning
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" /wd4018\")\n", app->name.c_str()); // disable 'signed/unsigned mismatch' warning
					
					sb.Append("\n");
				}
				
			#if 0
				if (s_platform == "macos")
				{
					sb.AppendFormat("set_target_properties(%s PROPERTIES MACOSX_BUNDLE_INFO_PLIST \"%s\")\n",
						app->name.c_str(),
						"/Users/thecat/framework/chibi/AppleInfo.plist");
					sb.Append("\n");
				}
			#endif

				if (s_platform == "macos")
				{
					// add rpath to the generated executable so that it can find dylibs inside the location of the executable itself. this is needed when copying generated shared libraries into the app bundle
					
					const char * conditional = "$<$<NOT:$<CONFIG:Distribution>>:echo>";
				
				// fixme : cmake is broken and always runs the custom command, regardless of whether the DEPENDS target is dirty or not. this causes install_name_tool to fail, as the rpath has already been set. I've appended "|| true" at the end of the command, to effectively ignore the return code from install_name_tool. a nasty side effect of this is we don't know whether the command succeeded or actually failed for some valid reason.. so ideally this hack is removed once cmake's behavior is fixed
				
					sb.AppendFormat(
						"add_custom_command(\n" \
							"\tTARGET %s POST_BUILD\n" \
							"\tCOMMAND %s install_name_tool -add_rpath \"@executable_path\" \"${BUNDLE_PATH}/Contents/MacOS/%s\" || true\n" \
							"\tDEPENDS %s)\n",
						app->name.c_str(),
						conditional,
						app->name.c_str(),
						app->name.c_str());
					
					sb.Append("\n");
				}
				
				if (!output(f, sb))
					return false;
			}
			
			for (auto & library : libraries)
			{
				bool empty = true;
				
				StringBuilder sb;
				
				sb.AppendFormat("# --- source group memberships for %s ---\n", library->name.c_str());
				sb.Append("\n");
				
			#if 1
				std::map<std::string, std::vector<ChibiLibraryFile*>> files_by_group;

				for (auto & file : library->files)
				{
					auto & group_files = files_by_group[file.group];
					
					group_files.push_back(&file);
				}
				
				for (auto & group_files_itr : files_by_group)
				{
					auto & group = group_files_itr.first;
					auto & files = group_files_itr.second;
				
				#if 0
					printf("group: %s\n", group.c_str());
				#endif
					
					sb.AppendFormat("source_group(\"%s\" FILES", group.c_str());
					
					for (auto & file : files)
						sb.AppendFormat("\n\t\"%s\"", file->filename.c_str());
					
					sb.Append(")\n");
					sb.Append("\n");
				
					empty = false;
				}
			#else
				for (auto & file : library->files)
				{
					if (file.group.empty())
					{
						sb.AppendFormat("source_group(\"%s\" FILES \"%s\")\n", "", file.filename.c_str());
						empty = false;
						continue;
					}
					
					sb.AppendFormat("source_group(\"%s\" FILES \"%s\")\n", file.group.c_str(), file.filename.c_str());
				
					empty = false;
				}
				
				sb.Append("\n");
			#endif
				
				if (empty == false)
				{
					if (!output(f, sb))
						return false;
				}
			}
			
			f.close();
		}
		
		return true;
	}
};

bool find_chibi_build_root(const char * source_path, char * build_root, const int build_root_size)
{
	// recursively find build_root
	
	char current_path[PATH_MAX];
	if (!copy_string(current_path, sizeof(current_path), source_path))
	{
		report_error(nullptr, "failed to copy path");
		return false;
	}
	
	assert(build_root_size > 0);
	build_root[0] = 0;

	for (;;)
	{
		char root_path[PATH_MAX];
		if (!concat(root_path, sizeof(root_path), current_path, "/chibi-root.txt"))
		{
			report_error(nullptr, "failed to create absolute path");
			return false;
		}

		if (file_exist(root_path))
		{
			if (!copy_string(build_root, build_root_size, root_path))
			{
				report_error(nullptr, "failed to copy path");
				return false;
			}
		}
		
		char * term = strrchr(current_path, '/');

		if (term == nullptr)
			break;
		else
			*term = 0;
	}
	
	if (build_root[0] == 0)
		return false;
	
	return true;
}

bool chibi_process(char * cwd, const char * src_path, const char * dst_path, const char ** targets, const int numTargets)
{
	for (int i = 0; i < numTargets; ++i)
		s_chibiInfo.build_targets.insert(targets[i]);
	
	if (cwd[0] == 0)
	{
		// get the current working directory. this is the root of our operations

		if (getcwd(cwd, PATH_MAX) == nullptr)
		{
			report_error(nullptr, "failed to get current working directory");
			return false;
		}
	}
	
	for (int i = 0; cwd[i] != 0; ++i)
		if (cwd[i] == '\\')
			cwd[i] = '/';
	
	// create the current absolute path given the source path command line option and the current working directory
	
	char source_path[PATH_MAX];
	
	if (!strcmp(src_path, "."))
	{
		if (!concat(source_path, sizeof(source_path), cwd))
		{
			report_error(nullptr, "failed to create absolute path");
			return false;
		}
	}
	else if (string_starts_with(src_path, "./"))
	{
		src_path += 2;
		
		if (!concat(source_path, sizeof(source_path), cwd, "/", src_path))
		{
			report_error(nullptr, "failed to create absolute path");
			return false;
		}
	}
	else
	{
		if (!copy_string(source_path, sizeof(source_path), src_path))
		{
			report_error(nullptr, "failed to copy current path");
			return false;
		}
	}

	// set the platform name
	
#if defined(MACOS)
	s_platform = "macos";
#elif defined(LINUX)
	s_platform = "linux";
#elif defined(WINDOWS)
	s_platform = "windows";
#else
	#error unknown platform
#endif

#if defined(LINUX)
	{
		bool isRaspberryPi = false;
		
		FILE * f = fopen("/proc/device-tree/model", "rt");
		
		if (f != nullptr)
		{
			char * line = nullptr;
			size_t lineSize = 0;
			
			const ssize_t r = getline(&line, &lineSize, f);
			
			if (r >= 0)
			{
				if (strstr(line, "Raspberry Pi") != nullptr)
					isRaspberryPi = true;
			}
			
			if (line != nullptr)
			{
				free(line);
				line = nullptr;
			}
			
			fclose(f);
			f = nullptr;
		}
		
		if (isRaspberryPi)
			s_platform_full = "linux.raspberry-pi";
	}
#endif

	// recursively find build_root
	
	char build_root[PATH_MAX];
	
	if (find_chibi_build_root(source_path, build_root, sizeof(build_root)) == false)
	{
		report_error(nullptr, "failed to find chibi-root.txt file");
		return false;
	}
	
#if 1
	printf("source_path: %s\n", source_path);
	printf("destination_path: %s\n", dst_path);
	printf("build_root: %s\n", build_root);
#endif

	if (!process_chibi_file(build_root))
	{
		report_error(nullptr, "an error occured while scanning for chibi files");
		return false;
	}

	if (s_currentGroup.empty() == false)
	{
		// todo : detect missing pop_group within the scope of a chibi file
		report_error(nullptr, "missing one or more 'pop_group'");
		return false;
	}
	
	//s_chibiInfo.dump_info();
	
	char output_filename[PATH_MAX];
	
	if (!concat(output_filename, sizeof(output_filename), dst_path, "/", "CMakeLists.txt"))
	{
		report_error(nullptr, "failed to create absolute path");
		return false;
	}
	
	CMakeWriter writer;
	
	if (!writer.write(output_filename))
	{
		report_error(nullptr, "an error occured while generating cmake file");
		return false;
	}

	return true;
}
