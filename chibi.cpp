#include "chibi-internal.h"
#include "filesystem.h"
#include "stringhelpers.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <vector>

// for MacOS users : "brew install ccache"

using namespace chibi;
using namespace chibi_filesystem;

namespace chibi
{
	bool write_cmake_file(const ChibiInfo & chibi_info, const char * platform, const char * output_filename);
	
	bool write_gradle_files(const ChibiInfo & chibi_info, const char * output_path);
}

// todo : add basic wildcard support ("include/*.cpp")

// todo : create library targets which are an alias for an existing system library, such as libusb, libsdl2, etc -> will allow to normalize library names, and to use either the system version or compile from source version interchangable

// todo : add option to specify plist file for apps
//            set_plist_file <filename>
// todo : add option to specify permissions (camera, microphone input),
//            add_permission camera
//            add_permission microphone

#ifdef _MSC_VER
	#include <direct.h>
	#include <stdint.h>
	#include <Windows.h>
	#ifndef PATH_MAX
		#define PATH_MAX _MAX_PATH
	#endif
	typedef SSIZE_T ssize_t;
	static ssize_t my_getline(char ** _line, size_t * _line_size, FILE * file)
	{
		char *& line = *_line;
		size_t & line_size = *_line_size;

		if (line == nullptr)
		{
			line_size = 4096;
			line = (char*)malloc(line_size);
		}

		for (;;)
		{
			auto pos = ftell(file);

			const char * r = fgets(line, (int)line_size, file);

			if (r == nullptr)
				break;

			const size_t length = strlen(line);

			if (length + 1 == line_size)
			{
				free(line);

				line_size *= 2;
				line = (char*)malloc(line_size);

				// for some reason, fseek fails to seek to the location from before the read, as determined by ftell above
				// ths _should_ work, but is broken on Windows. so instead I bumped the initial line size above as a work
				// around, and processing of files with lines larger than the initial size (which will re-alloc and re-read)
				// are likely to fail for now ..
				int seek_result = fseek(file, pos, SEEK_SET);

				if (seek_result != 0)
					return -1;
			}
			else
				return length;
		}

		return -1;
	}
#else
	#include <unistd.h>

	#define my_getline getline
#endif

#if defined(__GNUC__)
	#define sprintf_s(s, ss, f, ...) snprintf(s, ss, f, __VA_ARGS__)
	#define vsprintf_s(s, ss, f, a) vsnprintf(s, ss, f, a)
	#define strcpy_s(d, ds, s) strcpy(d, s)
	#define sscanf_s sscanf
#endif

static ssize_t s_current_line_length = 0; // fixme : make safe for concurrent use of library version of chibi

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

static std::vector<std::string> s_currentFile; // fixme : make safe for concurrent use of library version of chibi

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
	vsprintf_s(text, sizeof(text), format, ap);
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

static bool is_absolute_path(const char * path)
{
#if MACOS || LINUX
	// detect absolute Unix-style path
	return path[0] == '/';
#else
	// detect absolute Windows path
	for (int i = 0; path[i] != 0; ++i)
		if (path[i] == ':')
			return true;
	return false;
#endif
}

// fixme : make all of these safe for concurrent use of library version of chibi

static ChibiLibrary * s_currentLibrary = nullptr;

static std::string s_platform;
static std::string s_platform_full;

static bool is_platform(const char * platform)
{
	if (match_element(s_platform.c_str(), platform, '|'))
		return true;
	else if (s_platform_full.empty() == false && match_element(s_platform_full.c_str(), platform, '|'))
		return true;
	else
		return false;
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
	show_syntax_elem("with_platform <platform_name>[|<platform_name>..]", "with_platform may be specified in front of every line. when set, lines are filtered based on whether the current platform name matched the given platform name");
	
	printf("\n");
	printf("chibi syntax (within app or library context):\n");
	show_syntax_elem("add_dist_files <file>..", "adds one or more files to to be bundled with the application, when the build type is set to distribution");
	show_syntax_elem("add_files <file>.. [- [conglomerate <conglomerate_file>]]", "adds one or more files to compile. the list of files may optionally be terminated by '-', after which further options may be specified");
	show_syntax_elem("compile_definition <name> <value> [expose]", "adds a compile definition. when <value> is set to *, the compile definition is merely defined, without a value. when <expose> is set, the compile_definition is visible to all targets that depends on the current target");
	show_syntax_elem("depend_library <library_name> [local | global | find]", "adds a target dependency. <library_name> may refer to a chibi library target, or to a pre-built library or system library. when [local] is set, the file is interpreted as a pre-built library to be found at the given location, relative to the current chibi file. When [global] is set, the system-global library is used. When [find] is set, the library will be searched for on the system");
	show_syntax_elem("depend_package <package_name>", "depends on a package, to be found using one of cmake's find_package scripts. <package_name> defines the name of the cmake package script");
	show_syntax_elem("exclude_files <file>..", "exclude one or more files added before using add_files or scan_files");
	show_syntax_elem("group <group_name>", "specify the group for files to be added subsequently using add_files or scan_files");
	show_syntax_elem("header_path <path> [expose]", "specify a header search path. when [expose] is set, the search path will be propagated to all dependent targets");
	show_syntax_elem("resource_path <path>", "specify the resource_path. CHIBI_RESOURCE_PATH will be set appropriately to the given path for debug and release builds. for the distribution build type, files located at resource_path will be bundled with the app and CHIBI_RESOURCE_PATH will be set to the relative search path within the bundle");
	show_syntax_elem("license_file <path>", "specify license file(s) for a library");
	show_syntax_elem("scan_files <extension_or_wildcard> [path <path>].. [traverse] [group <group_name>] [conglomerate <conglomerate_file>]", "adds files by scanning the given path or the path of the current chibi file. files will be filtered using the extension or wildcard pattern provided. [path] can be used to specify a specific folder to look inside. [traverse] may be set to recursively look for files down the directory hierarchy. when [group] is specified, files found through the scan operation will be grouped by this name in generated ide project files. when [conglomerate] is set, the files will be concatenated into this files, and the generated file will be added instead. [conglomerate] may be used to speed up compile times by compiling a set of files in one go");
	show_syntax_elem("push_conglomerate <name>", "pushes a conglomerate file. files will automatically be added to the given conglomerate file. push_conglomerate must be followed by a matching pop_conglomerate");
	show_syntax_elem("link_translation_unit_using_function_call <function_name>", "adds a function to be called at the app level to ensure the translation unit in a dependent (static) library doesn't get stripped away by the linker");
}

static bool process_chibi_root_file(ChibiInfo & chibi_info, const char * filename, const std::string & current_group, const bool skip_file_scan);
static bool process_chibi_file(ChibiInfo & chibi_info, const char * filename, const std::string & current_group, const bool skip_file_scan);

static bool process_chibi_root_file(ChibiInfo & chibi_info, const char * filename, const std::string & current_group, const bool skip_file_scan)
{
	return process_chibi_file(chibi_info, filename, current_group, skip_file_scan);
}

static bool process_chibi_file(ChibiInfo & chibi_info, const char * filename, const std::string & current_group, const bool skip_file_scan)
{
	ChibiFileScope chibi_scope(filename);

	s_currentLibrary = nullptr;
	
	std::vector<std::string> group_stack;
	group_stack.push_back(current_group);
	
	std::vector<std::string> conglomerate_stack;
	
	char chibi_path[PATH_MAX];

	if (!get_path_from_filename(filename, chibi_path, PATH_MAX))
	{
		report_error(nullptr, "failed to get path from chibi filename: %s", filename);
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
			ssize_t r = my_getline(&line, &lineSize, f);

			if (r < 0)
			{
				s_current_line_length = 0;
				break;
			}
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
						{
							report_error(line, "failed to create absolute path");
							return false;
						}
						
						const int length = s_current_line_length;
						
						if (!process_chibi_file(chibi_info, chibi_file, group_stack.back(), skip_file_scan))
						{
							report_error(line, "failed to process chibi file: %s", chibi_file);
							return false;
						}
						
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
					
					group_stack.push_back(name);
				}
				else if (eat_word(linePtr, "pop_group"))
				{
					if (group_stack.size() == 1)
					{
						report_error(line, "no group left to pop");
						return false;
					}

					group_stack.pop_back();
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
						{
							report_error(line, "failed to create absolute path");
							return false;
						}
						
						const int length = s_current_line_length;
						
						if (!process_chibi_root_file(chibi_info, chibi_file, group_stack.back(), skip_file_scan))
							return false;
						
						s_current_line_length = length;
					}
				}
				else if (eat_word(linePtr, "library"))
				{
					s_currentLibrary = nullptr;
					
					const char * name;
					bool shared = false;
					bool prebuilt = false;
					bool objc_arc = false;
					
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
						else if (!strcmp(option, "prebuilt"))
							prebuilt = true;
						else if (!strcmp(option, "objc-arc"))
							objc_arc = true;
						else
						{
							report_error(line, "unknown option: %s", option);
							return false;
						}
					}
					
					if (chibi_info.library_exists(name))
					{
						report_error(line, "library already exists");
						return false;
					}
					
					ChibiLibrary * library = new ChibiLibrary();
					
					library->name = name;
					library->path = chibi_path;
					library->chibi_file = s_currentFile.back();
					
					if (group_stack.back().empty() == false)
						library->group_name = group_stack.back();
					
					library->shared = shared;
					library->prebuilt = prebuilt;
					library->objc_arc = objc_arc;
					
					chibi_info.libraries.push_back(library);
					
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
					
					if (chibi_info.library_exists(name))
					{
						report_error(line, "app already exists");
						return false;
					}
					
					ChibiLibrary * library = new ChibiLibrary();
					
					library->name = name;
					library->path = chibi_path;
					library->chibi_file = s_currentFile.back();
					
					if (group_stack.back().empty() == false)
						library->group_name = group_stack.back();
					
					library->isExecutable = true;
					
					chibi_info.libraries.push_back(library);
					
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
					
					chibi_info.cmake_module_paths.push_back(full_path);
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
						std::vector<ChibiLibraryFile> library_files;
						
						const char * group = nullptr;
						
						const char * conglomerate =
							conglomerate_stack.empty()
							? nullptr
							: conglomerate_stack.back().c_str();
						
						bool absolute = false;
						
						bool done = false;
						
						// parse file list
						
						for (;;)
						{
							const char * filename;
							
							if (!eat_word_v2(linePtr, filename))
							{
								done = true;
								break;
							}
							
							if (!strcmp(filename, "-"))
								break;
							
							ChibiLibraryFile file;
							
							file.filename = filename;
							
							library_files.push_back(file);
						}
						
						// parse options
						
						if (done == false)
						{
							for (;;)
							{
								const char * option;
								
								if (!eat_word_v2(linePtr, option))
								{
									done = true;
									break;
								}
								
								if (!strcmp(option, "group"))
								{
									if (!eat_word_v2(linePtr, group))
									{
										report_error(line, "missing group name");
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
								else if (!strcmp(option, "absolute"))
								{
									absolute = true;
								}
								else
								{
									report_error(line, "unknown option: %s", option);
									return false;
								}
							}
						}
						
						if (absolute == false)
						{
							for (auto & library_file : library_files)
							{
								char full_path[PATH_MAX];
								if (!concat(full_path, sizeof(full_path), chibi_path, "/", library_file.filename.c_str()))
								{
									report_error(line, "failed to create absolute path");
									return false;
								}
								
								library_file.filename = full_path;
							}
						}
						
						if (group != nullptr)
						{
							for (auto & library_file : library_files)
								library_file.group = group;
						}
						
						if (conglomerate != nullptr)
						{
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", conglomerate))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							for (auto & library_file : library_files)
							{
								library_file.conglomerate_filename = full_path;
								library_file.compile = false;
							}
							
							if (group != nullptr)
							{
								s_currentLibrary->conglomerate_groups[full_path] = group;
							}
						}
						
						s_currentLibrary->files.insert(
							s_currentLibrary->files.end(),
							library_files.begin(),
							library_files.end());
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
						
						const char * conglomerate =
							conglomerate_stack.empty()
							? nullptr
							: conglomerate_stack.back().c_str();
						
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
						
						if (skip_file_scan)
							continue;
						
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
									if (match_wildcard(filename.c_str(), extension, ';') == false)
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
						
						if (conglomerate != nullptr)
						{
							char full_path[PATH_MAX];
							if (!concat(full_path, sizeof(full_path), chibi_path, "/", conglomerate))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
							
							for (auto & library_file : library_files)
							{
								library_file.conglomerate_filename = full_path;
								library_file.compile = false;
							}
							
							if (group != nullptr)
							{
								s_currentLibrary->conglomerate_groups[full_path] = group;
							}
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
						
						bool absolute = false;
						
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
							else if (!strcmp(option, "absolute"))
							{
								absolute = true;
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
						
						if (absolute)
						{
							if (!concat(full_path, sizeof(full_path), path))
							{
								report_error(line, "failed to create absolute path");
								return false;
							}
						}
						else if (strcmp(path, ".") == 0)
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
					if (s_currentLibrary == nullptr)
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
				else if (eat_word(linePtr, "license_file"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "license_file without a target");
						return false;
					}
					else if (s_currentLibrary->isExecutable)
					{
						report_error(line, "license_file target is not a library");
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

						if (file_exist(full_path) == false)
						{
							report_error(line, "failed to find license file: %s", path);
							return false;
						}

						s_currentLibrary->license_files.push_back(path);
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
				else if (eat_word(linePtr, "push_conglomerate"))
				{
					const char * file;
					
					if (!eat_word_v2(linePtr, file))
					{
						report_error(line, "missing conglomerate file");
						return false;
					}
					
					conglomerate_stack.push_back(file);
				}
				else if (eat_word(linePtr, "pop_conglomerate"))
				{
					if (conglomerate_stack.empty())
					{
						report_error(line, "no conglomerate file left to pop");
						return false;
					}

					conglomerate_stack.pop_back();
				}
				else if (eat_word(linePtr, "link_translation_unit_using_function_call"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error(line, "link_translation_unit_using_function_call without a target");
						return false;
					}
					else
					{
						const char * function_name;
							
						if (!eat_word_v2(linePtr, function_name))
						{
							report_error(line, "missing function name");
							return false;
						}

						s_currentLibrary->link_translation_unit_using_function_calls.push_back(function_name);
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
						report_error(line, "unexpected text at end of line: %s", linePtr + i);
						return false;
					}
				}
			}
		}

		free(line);
		line = nullptr;
		
		f.close();
		
		if (group_stack.size() > 1)
		{
			char temp[512];
			temp[0] = 0;
			for (size_t i = 1; i < group_stack.size(); ++i)
				concat(temp, sizeof(temp), group_stack[i].c_str(), " ");
			report_error(nullptr, "missing one or more 'pop_group'. groups still active: %s", temp);
			return false;
		}
		
		return true;
	}
}

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

static bool create_absolute_path_given_cwd(const char * cwd, const char * path, char * out_path, const int out_path_size)
{
	// create the current absolute path given the source path command line option and the current working directory
	
	if (!strcmp(path, "."))
	{
		if (!concat(out_path, out_path_size, cwd))
		{
			return false;
		}
	}
	else if (string_starts_with(path, "./"))
	{
		path += 2;
		
		if (!concat(out_path, out_path_size, cwd, "/", path))
		{
			return false;
		}
	}
	else
	{
		if (!copy_string(out_path, out_path_size, path))
		{
			return false;
		}
	}
	
	return true;
}

static bool get_current_working_directory(char * out_cwd, const int out_cwd_size)
{
#if WINDOWS
	if (_getcwd(out_cwd, out_cwd_size) == nullptr)
		return false;
#else
	if (getcwd(out_cwd, out_cwd_size) == nullptr)
		return false;
#endif
	
	// normalize path separators
	
	for (int i = 0; out_cwd[i] != 0; ++i)
		if (out_cwd[i] == '\\')
			out_cwd[i] = '/';
	
	return true;
}

static bool chibi_process(ChibiInfo & chibi_info, const char * build_root, const bool skip_file_scan, const char * platform)
{
	// set the platform name
	
	if (platform != nullptr)
	{
		const char * separator = strchr(platform, '.');
		
		if (separator == nullptr)
		{
			s_platform = platform;
			s_platform_full.clear();
		}
		else
		{
			s_platform = std::string(platform).substr(0, separator - platform);
			s_platform_full = platform;
		}
	}
	else
	{
	#if defined(MACOS)
		s_platform = "macos";
	#elif defined(LINUX)
		s_platform = "linux";
	#elif defined(WINDOWS)
		s_platform = "windows";
	#elif defined(ANDROID)
		s_platform = "android";
	#else
		#error unknown platform
	#endif

	#if defined(LINUX)
		bool isRaspberryPi = false;
		
		FILE * f = fopen("/proc/device-tree/model", "rt");
		
		if (f != nullptr)
		{
			char * line = nullptr;
			size_t lineSize = 0;
			
			const ssize_t r = my_getline(&line, &lineSize, f);
			
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
	#endif
	}

	std::string current_group;
	
	if (!process_chibi_root_file(chibi_info, build_root, current_group, skip_file_scan))
	{
		report_error(nullptr, "an error occured while scanning for chibi files");
		return false;
	}
	
	//s_chibiInfo.dump_info();
	
	return true;
}

bool chibi_generate(const char * in_cwd, const char * src_path, const char * dst_path, const char ** targets, const int numTargets, const char * platform)
{
	ChibiInfo chibi_info;
	
	for (int i = 0; i < numTargets; ++i)
		chibi_info.build_targets.insert(targets[i]);
	
	//
	
	char cwd[PATH_MAX];
	
	if (in_cwd == nullptr || in_cwd[0] == 0)
	{
		// get the current working directory. this is the root of our operations

		if (get_current_working_directory(cwd, sizeof(cwd)) == false)
		{
			report_error(nullptr, "failed to get current working directory");
			return false;
		}
	}
	else
	{
		if (!copy_string(cwd, sizeof(cwd), in_cwd))
		{
			report_error(nullptr, "failed to copy cwd string");
			return false;
		}
	}
	
	//
	
	char source_path[PATH_MAX];
	
	if (create_absolute_path_given_cwd(cwd, src_path, source_path, sizeof(source_path)) == false)
	{
		report_error(nullptr, "failed to create absolute path");
		return false;
	}

	
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

	if (chibi_process(chibi_info, build_root, false, platform) == false)
		return false;
	
	printf("found %d libraries and apps in total\n", (int)chibi_info.libraries.size());

	// write cmake file
	
	char output_filename[PATH_MAX];
	
	if (!concat(output_filename, sizeof(output_filename), dst_path, "/", "CMakeLists.txt"))
	{
		report_error(nullptr, "failed to create absolute path");
		return false;
	}
	
	if (!write_cmake_file(
		chibi_info,
		!s_platform_full.empty()
			? s_platform_full.c_str()
			: s_platform.c_str(),
		output_filename))
	{
		report_error(nullptr, "an error occured while generating cmake file");
		return false;
	}

	// write gradle files
	
	if (is_platform("android"))
	{
		if (!write_gradle_files(chibi_info, dst_path))
		{
			report_error(nullptr, "an error occured while generating gradle files");
			return false;
		}
	}

	return true;
}

bool list_chibi_targets(const char * build_root, std::vector<std::string> & library_targets, std::vector<std::string> & app_targets)
{
	ChibiInfo chibi_info;
	
	//
	
	if (chibi_process(chibi_info, build_root, true, nullptr) == false)
		return false;
	
	//
	
	for (auto * library : chibi_info.libraries)
	{
		if (library->isExecutable)
			app_targets.push_back(library->name);
		else
			library_targets.push_back(library->name);
	}
	
	return true;
}
