#include "Debugging.h"
#include "filesystem.h"
#include "Path.h"
#include "StringBuilder.h"
#include "StringEx.h"

#include <limits.h>
#include <unistd.h>
#include <set>
#include <string>
#include <vector>

// todo : redesign the embed_framework option

#define STRING_BUFFER_SIZE (1 << 14)

#define strcpy_s(d, l, s) strcpy(d, s)
#define strcat_s(d, l, s) strcat(d, s)

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

static bool concat(char * dst, int dstSize, const char * s1, const char * s2 = nullptr, const char * s3 = nullptr, const char * s4 = nullptr)
{
	return
		do_concat(dst, dstSize, s1) &&
		(s2 == nullptr || do_concat(dst, dstSize, s2)) &&
		(s3 == nullptr || do_concat(dst, dstSize, s3)) &&
		(s4 == nullptr || do_concat(dst, dstSize, s4));
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

struct ChibiLibraryFile
{
	std::string filename;
	
	std::string group;
};

struct ChibiLibraryDependency
{
	enum Type
	{
		kType_Undefined,
		kType_Generated,
		kType_Local,
		kType_Find
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
	
	bool isExecutable = false;
	
	std::vector<ChibiLibraryFile> files;
	
	std::vector<ChibiLibraryDependency> library_dependencies;
	
	std::vector<std::string> package_dependencies;
	
	std::vector<ChibiHeaderPath> header_paths;
	
	std::vector<ChibiCompileDefinition> compile_definitions;
	
	std::string resource_path;
	
	void dump_info() const
	{
		printf("library: %s\n", name.c_str());
		
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

static bool process_chibi_file(const char * filename)
{
	char chibi_path[PATH_MAX];

	if (!get_path_from_filename(filename, chibi_path, PATH_MAX))
	{
		report_error(nullptr, "failed to get path from chibi filename");
		return false;
	}
	
	//
	
	FILE * f = fopen(filename, "rt");

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
						if (!process_chibi_file(chibi_file))
							return false;
						s_current_line_length = length;
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
								if (Path::GetExtension(filename, true) != extension)
									return true;
							
								if (platform != nullptr && platform != s_platform)
									return true;
								
								for (auto & excluded_path : excluded_paths)
									if (String::StartsWith(filename, excluded_path))
										return true;
								
								return false;
							});
						
						filenames.erase(end, filenames.end());
						
						if (merge_into != nullptr)
						{
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", merge_into))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							FILE * target_file = fopen(full_path, "wt");
							
							if (target_file == nullptr)
							{
								report_error(line, "failed to open merge_into target");
								return false;
							}
							else
							{
								for (auto & filename : filenames)
								{
									FILE * source_file = fopen(filename.c_str(), "rt");
									
									if (source_file == nullptr)
									{
										report_error(line, "failed to open  file: %s", filename.c_str());
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
									
									fclose(source_file);
									source_file = nullptr;
								}
								
								fclose(target_file);
								target_file = nullptr;
							}
							
							filenames.clear();
							
							filenames.push_back(full_path);
						}
						
						if (conglomerate != nullptr)
						{
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", conglomerate))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							FILE * target_file = fopen(full_path, "wt");
							
							if (target_file == nullptr)
							{
								report_error(line, "failed to open conglomerate target");
								return false;
							}
							else
							{
								fprintf(target_file, "// auto-generated. do not hand-edit\n\n");
								
								for (auto & filename : filenames)
								{
									fprintf(target_file, "#include \"%s\"\n", filename.c_str());
								}
								
								fclose(target_file);
								target_file = nullptr;
							}
							
							filenames.clear();
							
							filenames.push_back(full_path);
						}
						
						for (auto & filename : filenames)
						{
						#if 1
							bool isExcluded = false;
							
							for (auto & excluded_path : excluded_paths)
								if (String::StartsWith(filename, excluded_path))
									isExcluded = true;
							
							if (isExcluded)
							{
								report_error(line, "???");
								return false;
							}
						#endif
							
							ChibiLibraryFile file;
							
							file.filename = filename;
							
							if (group != nullptr)
								file.group = group;
							
							s_currentLibrary->files.push_back(file);
						}
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
		if (library.isExecutable)
			printf("handle_app: %s\n", library.name.c_str());
		else
			printf("handle_library: %s\n", library.name.c_str());
		
		traversed_libraries.insert(library.name);
		
		// recurse library dependencies
		
		for (auto & library_dependency : library.library_dependencies)
		{
			if (traversed_libraries.count(library_dependency.name) != 0)
				continue;
			
			if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
			{
				ChibiLibrary * library = s_chibiInfo.find_library(library_dependency.name.c_str());
				
				if (library == nullptr)
				{
					report_error(nullptr, "failed to find library dependency: %s", library_dependency.name.c_str());
					return false;
				}
				else
				{
					if (handle_library(*library, traversed_libraries, libraries) == false)
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
			for (auto & library_dependency : library.library_dependencies)
			{
				if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
				{
					sb.AppendFormat("target_link_libraries(%s PRIVATE %s)\n",
						library.name.c_str(),
						library_dependency.name.c_str());
				}
				else if (library_dependency.type == ChibiLibraryDependency::kType_Local)
				{
					sb.AppendFormat("target_link_libraries(%s PRIVATE %s)\n",
						library.name.c_str(),
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
					
					sb.AppendFormat("find_library(%s %s)\n", var_name, library_dependency.name.c_str());
					
					sb.AppendFormat("target_link_libraries(%s PRIVATE ${%s})\n",
						library.name.c_str(),
						var_name);
				}
				else
				{
					report_error(nullptr, "internal error: unknown library dependency type");
					return false;
				}
				
				if (library_dependency.embed_framework)
				{
				// fixme : this is just plain ugly
					sb.AppendFormat("file(COPY \"%s\" DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Debug/)\n", library_dependency.path.c_str());
					sb.AppendFormat("file(COPY \"%s\" DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/Release/)\n", library_dependency.path.c_str());
				}
			}
			
			sb.Append("\n");
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
				sb.Append("find_package(PkgConfig)\n");
				
				sb.AppendFormat("find_package(%s REQUIRED)\n", package_dependency.c_str());
				
				sb.AppendFormat("target_include_directories(%s PRIVATE %s \"${%s_INCLUDE_DIRS}\")\n",
					library.name.c_str(),
					library.path.c_str(),
					package_dependency.c_str());
				
				sb.AppendFormat("target_link_libraries(%s PRIVATE ${%s_LIBRARIES})\n",
					library.name.c_str(),
					package_dependency.c_str());
			}
			
			sb.Append("\n");
		}
		
		return true;
	}
	
	template <typename S>
	static bool output(FILE * f, S & sb)
	{
		if (!sb.IsValid())
		{
			report_error(nullptr, "output buffer overflow");
			return false;
		}
		
		if (fprintf(f, "%s", sb.ToString()) < 0)
		{
			report_error(nullptr, "failed to write to disk");
			return false;
		}
		
		return true;
	}
	
	bool write()
	{
		// gather the targets to emit
		
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
				[](auto & a, auto & b)
				{
					return a.filename < b.filename;
				});
		}
		
		// write CMake output
		
	// fixme : output path
		const char * output_filename = "/Users/thecat/chibi/output/CMakeLists.txt";
		FILE * f = fopen(output_filename, "wt");
		
		if (f == nullptr)
		{
			report_error(nullptr, "failed to open output file: %s", output_filename);
			return false;
		}
		else
		{
			{
				StringBuilder<STRING_BUFFER_SIZE> sb;
				
				sb.Append("cmake_minimum_required(VERSION 2.6)\n");
				sb.Append("\n");
				sb.Append("set(CMAKE_CXX_STANDARD 14)\n");
				sb.Append("\n");
				
				if (!s_cmake_module_paths.empty())
				{
					for (auto & cmake_module_path : s_cmake_module_paths)
						sb.AppendFormat("list(APPEND CMAKE_MODULE_PATH \"%s\")\n", cmake_module_path.c_str());
					sb.Append("\n");
				}
				
				if (!output(f, sb))
					return false;
			}
			
			for (auto & library : libraries)
			{
				if (library->isExecutable)
					continue;
				
				StringBuilder<STRING_BUFFER_SIZE> sb;
				
				sb.AppendFormat("# --- library %s ---\n", library->name.c_str());
				sb.Append("\n");
				sb.Append("add_library(");
				sb.Append(library->name.c_str());
				
				for (auto & file : library->files)
				{
					sb.Append("\n\t");
					sb.AppendFormat("\"%s\"", file.filename.c_str());
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
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
				
				StringBuilder<STRING_BUFFER_SIZE> sb;
				
				sb.AppendFormat("# --- app %s ---\n", app->name.c_str());
				sb.Append("\n");
				sb.Append("add_executable(");
				sb.Append(app->name.c_str());
				
				for (auto & file : app->files)
				{
					sb.Append("\n\t");
					sb.AppendFormat("\"%s\"", file.filename.c_str());
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
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
				
				if (!output(f, sb))
					return false;
			}
			
			for (auto & library : libraries)
			{
				bool empty = true;
				
				StringBuilder<STRING_BUFFER_SIZE> sb;
				
				sb.Append("# --- source group memberships ---\n");
				sb.Append("\n");
				
				for (auto & file : library->files)
				{
					if (file.group.empty())
						continue;
					
					sb.AppendFormat("source_group(\"%s\" FILES %s)\n", file.group.c_str(), file.filename.c_str());
					
					empty = false;
				}
				
				sb.Append("\n");
				
				if (empty == false)
				{
					if (!output(f, sb))
						return false;
				}
			}
			
			fclose(f);
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
	
	bool run_cmake = false;
	bool run_build = false;
	
	argc -= 1;
	argv += 1;
	
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
	strcpy_s(current_path, sizeof(current_path), cwd);

	char build_root[PATH_MAX];
	memset(build_root, 0, sizeof(build_root));

	for (;;)
	{
		char root_path[PATH_MAX];
		strcpy_s(root_path, sizeof(root_path), current_path);
		strcat_s(root_path, sizeof(root_path), "/chibi_root");

		if (file_exist(root_path))
		{
			strcpy_s(build_root, sizeof(build_root), root_path);
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

	// build_root should be valid here
	Assert(file_exist(build_root));

	if (!process_chibi_file(build_root))
	{
		report_error(nullptr, "an error occured while scanning for chibi files");
		return -1;
	}
	
	s_chibiInfo.dump_info();
	
	CMakeWriter writer;
	
	writer.write();

	return 0;
}
