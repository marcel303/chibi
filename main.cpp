#include "filesystem.h"

#include <algorithm>
#include <deque>
#include <limits.h>
#include <map>
#include <set>
#include <stdarg.h>
#include <string>
#include <vector>

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

static bool eat_word_v2(char *& line, const char *& word)
{
	while (*line != 0 && is_whitespace(*line) == true)
		line++;
	
	if (*line == 0)
		return false;
	
	word = line;
	
	while (*line != 0 && is_whitespace(*line) == false)
		line++;
	
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

static bool eat_word(char *& line, const char * word)
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
	
	if (is_whitespace(line[index]) == false)
		return false;
	else
	{
		line += index;
		return true;
	}
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

struct ChibiHeaderPath
{
	std::string path;
	
	bool expose = false;
};

struct ChibiCompileDefinition
{
	std::string name;
	std::string value;
	
	bool expose = false;
	
	std::string toolchain;
};

struct ChibiLibrary
{
	std::string name;
	std::string path;
	std::string group_name;
	
	bool isExecutable = false;
	
	std::vector<ChibiLibraryFile> files;
	
	std::vector<ChibiLibraryDependency> library_dependencies;
	
	std::vector<std::string> package_dependencies;
	
	std::vector<ChibiHeaderPath> header_paths;
	
	std::vector<ChibiCompileDefinition> compile_definitions;
	
	std::string resource_path;

	std::vector<std::string> dist_files;
	
	void dump_info() const
	{
		printf("%s: %s\n", isExecutable ? "app" : "library", name.c_str());
		
		for (auto & file : files)
		{
			printf("\tfile: %s\n", file.filename.c_str());
		}
		
		for (auto & header_path : header_paths)
		{
			printf("\theader path: %s\n", header_path.path.c_str());
		}
	}
};

struct ChibiInfo
{
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

static std::vector<std::string> s_cmake_module_paths;

static std::string s_currentGroup;

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

static bool process_chibi_file(const char * filename)
{
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
					
					if (platform != s_platform)
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
						const std::string group = s_currentGroup;
						
						if (!process_chibi_file(chibi_file))
							return false;
						
						s_currentLibrary = nullptr;
						
						s_current_line_length = length;
						s_currentGroup = group;
					}
				}
				else if (eat_word(linePtr, "global_group"))
				{
					const char * name;
					
					if (!eat_word_v2(linePtr, name))
					{
						report_error(line, "missing group name");
						return false;
					}
					
					s_currentGroup = name;
					
					if (s_currentLibrary != nullptr)
					{
						s_currentLibrary->group_name = name;
					}
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
						
						if (!concat(chibi_file, sizeof(chibi_file), chibi_path, "/", location, "/chibi_root"))
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
					
					if (!eat_word_v2(linePtr, name))
					{
						report_error(line, "missing name");
						return false;
					}
						
					if (s_chibiInfo.library_exists(name))
					{
						report_error(line, "library already exists");
						return false;
					}
					
					ChibiLibrary * library = new ChibiLibrary();
					
					library->name = name;
					library->path = chibi_path;
					
					if (s_currentGroup.empty() == false)
						library->group_name = s_currentGroup;
					
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
					
					if (s_currentGroup.empty() == false)
						library->group_name = s_currentGroup;
					
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
						
						auto end = std::remove_if(filenames.begin(), filenames.end(), [&](const std::string & filename) -> bool
							{
								if (get_path_extension(filename, true) != extension)
									return true;
							
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
										report_error(line, "failed to open  file: %s", library_file.filename.c_str());
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
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error(line, "missing name");
							return false;
						}
						
						s_currentLibrary->package_dependencies.push_back(name);
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
							else
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						if (platform != nullptr && platform != s_platform)
							continue;
							
						char full_path[PATH_MAX];
						if (!concat(full_path, sizeof(full_path), chibi_path, "/", path))
						{
							report_error(line, "failed to create absolute path");
							return false;
						}
						
						ChibiHeaderPath header_path;
						header_path.path = full_path;
						header_path.expose = expose;
						
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
						
						if (!eat_word_v2(linePtr, path))
						{
							report_error(line, "missing path");
							return false;
						}
						
						char full_path[PATH_MAX];
						if (!concat(full_path, sizeof(full_path), chibi_path, "/", path))
						{
							report_error(line, "failed to create absolute path");
							return false;
						}
						
						s_currentLibrary->resource_path = full_path;
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
					header_path.path.c_str());
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
				
				if (compile_definition.value.empty())
				{
					sb.AppendFormat("target_compile_definitions(%s %s %s)\n",
						library.name.c_str(),
						visibility,
						compile_definition.name.c_str());
				}
				else
				{
					sb.AppendFormat("target_compile_definitions(%s %s %s=%s)\n",
						library.name.c_str(),
						visibility,
						compile_definition.name.c_str(),
						compile_definition.value.c_str());
				}
				
				if (toolchain != nullptr)
				{
					sb.AppendFormat("endif (%s)\n", toolchain);
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
	
	template <typename S>
	static bool write_package_dependencies(S & sb, const ChibiLibrary & library)
	{
		if (!library.package_dependencies.empty())
		{
			for (auto & package_dependency : library.package_dependencies)
			{
				sb.AppendFormat("find_package(%s REQUIRED)\n", package_dependency.c_str());
			}
			sb.Append("\n");
			
			for (auto & package_dependency : library.package_dependencies)
			{
				sb.AppendFormat("target_include_directories(%s PRIVATE %s \"${%s_INCLUDE_DIRS}\")\n",
					library.name.c_str(),
					library.path.c_str(),
					package_dependency.c_str());
			}
			sb.Append("\n");
			
			for (auto & package_dependency : library.package_dependencies)
			{
				sb.AppendFormat("target_link_libraries(%s PRIVATE ${%s_LIBRARIES})\n",
					library.name.c_str(),
					package_dependency.c_str());
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
				// todo : use CMAKE_RUNTIME_OUTPUT_DIRECTORY instead of binary_dir + config ?
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
			
			//if (true) // todo : check if we want to build this library or not
			if (library->isExecutable)
			{
				if (handle_library(*library, traversed_libraries, libraries) == false)
					return false;
			}
		}
		
		for (auto & library : s_chibiInfo.libraries)
		{
			if (traversed_libraries.count(library->name) != 0)
				continue;
			
			if (handle_library(*library, traversed_libraries, libraries) == false)
				return false;
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
			{
				StringBuilder sb;
				
				sb.Append("cmake_minimum_required(VERSION 3.6)\n");
				sb.Append("\n");
				sb.Append("set(CMAKE_CXX_STANDARD 11)\n");
				sb.Append("\n");
				
				sb.Append("set_property(GLOBAL PROPERTY USE_FOLDERS ON)\n");
				sb.Append("\n");
				
				if (!s_cmake_module_paths.empty())
				{
					for (auto & cmake_module_path : s_cmake_module_paths)
						sb.AppendFormat("list(APPEND CMAKE_MODULE_PATH \"%s\")\n", cmake_module_path.c_str());
					sb.Append("\n");
				}
				
				sb.Append("if (APPLE)\n");
				sb.Append("\tfind_package(PkgConfig REQUIRED)\n");
				sb.Append("endif (APPLE)\n");
				sb.Append("\n");
				
				sb.Append("set(CMAKE_MACOSX_RPATH ON)\n");
				sb.Append("\n");

				sb.Append("if ((CMAKE_CXX_COMPILER_ID MATCHES \"MSVC\") AND NOT CMAKE_CL_64)\n");
				sb.Append("\tadd_compile_options(/arch:SSE2)\n");
				sb.Append("\tadd_definitions(-D__SSE2__=1)\n");
				sb.Append("endif ()\n");
				sb.Append("\n");
				
				sb.Append("set(SOURCE_GROUP_DELIMITER \"/\")\n");
				sb.Append("\n");
				
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
				sb.Append("\n\tSTATIC");
				//sb.Append("\n\tSHARED");
				
				for (auto & file : library->files)
				{
					sb.Append("\n\t");
					sb.AppendFormat("\"%s\"", file.filename.c_str());
					
					if (file.compile == false)
						has_compile_disabled_files = true;
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
					sb.AppendFormat("target_compile_definitions(%s PRIVATE CHIBI_RESOURCE_PATH=\"%s\")\n",
						app->name.c_str(),
						app->resource_path.c_str());
					sb.Append("\n");
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
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY LINK_FLAGS \"/SAFESEH:NO\")", app->name.c_str());

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

static bool eat_arg(int & argc, const char **& argv, const char *& arg)
{
	if (argc == 0)
		return false;
	
	arg = *argv;
	
	argc -= 1;
	argv += 1;
	
	return true;
}

int main(int argc, const char * argv[])
{
	char cwd[PATH_MAX];
	cwd[0] = 0;
	
	const char * src_path = nullptr;
	const char * dst_path = nullptr;
	
	bool run_cmake = false;
	bool run_build = false;
	
	argc -= 1;
	argv += 1;
	
	if (!eat_arg(argc, argv, src_path))
	{
		report_error(nullptr, "missing source path");
		return -1;
	}
	
	if (!eat_arg(argc, argv, dst_path))
	{
		report_error(nullptr, "missing destination path");
		return -1;
	}
	
	while (argc > 0)
	{
		const char * option;
		
		if (!eat_arg(argc, argv, option))
			break;
		
		if (!strcmp(option, "-cmake"))
			run_cmake = true;
		else if (!strcmp(option, "-build"))
			run_build = true;
		else
		{
			report_error(nullptr, "unknown command line option: %s", option);
			return -1;
		}
	}
	
	if (cwd[0] == 0)
	{
		// get the current working directory. this is the root of our operations

		if (getcwd(cwd, PATH_MAX) == nullptr)
		{
			report_error(nullptr, "failed to get current working directory");
			return -1;
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
			return -1;
		}
	}
	else if (string_starts_with(src_path, "./"))
	{
		src_path += 2;
		
		if (!concat(source_path, sizeof(source_path), cwd, "/", src_path))
		{
			report_error(nullptr, "failed to create absolute path");
			return -1;
		}
	}
	else
	{
		if (!copy_string(source_path, sizeof(source_path), src_path))
		{
			report_error(nullptr, "failed to copy current path");
			return -1;
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
	
	// recursively find build_root
	
	char current_path[PATH_MAX];
	if (!copy_string(current_path, sizeof(current_path), source_path))
	{
		report_error(nullptr, "failed to copy path");
		return -1;
	}

	char build_root[PATH_MAX];
	memset(build_root, 0, sizeof(build_root));

	for (;;)
	{
		char root_path[PATH_MAX];
		if (!concat(root_path, sizeof(root_path), current_path, "/chibi_root"))
		{
			report_error(nullptr, "failed to create absolute path");
			return -1;
		}

		if (file_exist(root_path))
		{
			if (!copy_string(build_root, sizeof(build_root), root_path))
			{
				report_error(nullptr, "failed to copy path");
				return -1;
			}
		}
		
		char * term = strrchr(current_path, '/');

		if (term == nullptr)
			break;
		else
			*term = 0;
	}
	
	if (build_root[0] == 0)
	{
		report_error(nullptr, "failed to find chibi_root file");
		return -1;
	}
	
#if 1
	printf("source_path: %s\n", source_path);
	printf("destination_path: %s\n", dst_path);
	printf("build_root: %s\n", build_root);
#endif

	if (!process_chibi_file(build_root))
	{
		report_error(nullptr, "an error occured while scanning for chibi files");
		return -1;
	}
	
	//s_chibiInfo.dump_info();
	
	char output_filename[PATH_MAX];
	
	if (!concat(output_filename, sizeof(output_filename), dst_path, "/", "CMakeLists.txt"))
	{
		report_error(nullptr, "failed to create absolute path");
		return -1;
	}
	
	CMakeWriter writer;
	
	writer.write(output_filename);

	return 0;
}
