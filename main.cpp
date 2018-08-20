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

#define strcpy_s(d, l, s) strcpy(d, s)
#define strcat_s(d, l, s) strcat(d, s)

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

static bool eat_word_v2(char *& line, char *& word)
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
	
	if (line != nullptr)
		printf(">> %s\n", line);

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
};

struct ChibiLibrary
{
	std::string name;
	std::string path;
	
	bool isExecutable = false;
	
	std::vector<ChibiLibraryFile> files;
	
	std::vector<std::string> library_dependencies;
	
	std::vector<std::string> package_dependencies;
	
	std::vector<ChibiHeaderPath> header_paths;
	
	std::vector<ChibiCompileDefinition> compile_definitions;
	
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
				
				if (is_comment_or_whitespace(line))
					continue;
				
				char * linePtr = line;
				
				if (eat_word(linePtr, "with_platform"))
				{
					char * platform;
					
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
					char * location;
					
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
						
						if (!process_chibi_file(chibi_file))
							return false;
					}
				}
				else if (eat_word(linePtr, "library"))
				{
					s_currentLibrary = nullptr;
					
					char * name;
					
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
					
					char * name;
					
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
							char * filename;
							
							if (!eat_word_v2(linePtr, filename))
								break;
							
							ChibiLibraryFile file;
							
							file.filename = filename;
							
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
						char * extension;
						
						if (!eat_word_v2(linePtr, extension))
						{
							report_error(line, "missing extension");
							return false;
						}
						
						bool traverse = false;
						
						std::vector<std::string> excluded_paths;
						
						char * platform = nullptr;
						
						for (;;)
						{
							char * option;
							
							if (eat_word_v2(linePtr, option) == false)
								break;
							
							if (!strcmp(option, "traverse"))
								traverse = true;
							else if (!strcmp(option, "platform"))
							{
								if (!eat_word_v2(linePtr, platform))
									report_error(line, "missing platform name");
							}
							else if (!strcmp(option, "exclude_path"))
							{
								char * excluded_path;
								
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
							else
							{
								report_error(line, "unknown option: %s", option);
								return false;
							}
						}
						
						// scan files
						
						auto filenames = listFiles(chibi_path, traverse);
						
						for (auto & filename : filenames)
						{
							if (Path::GetExtension(filename, true) != extension)
								continue;
							
							if (platform != nullptr && platform != s_platform)
								continue;
							
							bool isExcluded = false;
							
							for (auto & excluded_path : excluded_paths)
								if (String::StartsWith(filename, excluded_path))
									isExcluded = true;
							
							if (isExcluded)
								continue;
							
							ChibiLibraryFile file;
							
							file.filename = filename;
							
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
							char * filename;
							
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
						char * name;
						
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
						char * name;
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error(line, "missing name");
							return false;
						}
						
						s_currentLibrary->library_dependencies.push_back(name);
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
						char * path;
						
						if (!eat_word_v2(linePtr, path))
						{
							report_error(line, "missing path");
							return false;
						}
						
						bool expose = false;
						
						char * platform = nullptr;
						
						for (;;)
						{
							char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
							if (!strcmp(option, "expose"))
								expose = true;
							else if (!strcmp(option, "platform"))
							{
								if (!eat_word_v2(linePtr, platform))
									report_error(line, "missing platform name");
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
						char * name;
						char * value;
						
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
						
						for (;;)
						{
							char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
							if (!strcmp(option, "expose"))
								expose = true;
						}
						
						ChibiCompileDefinition compile_definition;
						
						compile_definition.name = name;
						compile_definition.value = value;
						compile_definition.expose = expose;
						
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
						char * path;
						
						if (!eat_word_v2(linePtr, path))
						{
							report_error(line, "missing path");
							return false;
						}
					}
				}
				else
				{
					report_error(line, "syntax error");
					return false;
				}
			}
		}

		free(line);
		line = nullptr;

		return true;
	}
}

struct CMakeWriter
{
	bool handle_library(ChibiLibrary & library, std::set<std::string> & traversed_libraries, std::vector<ChibiLibrary*> & libraries)
	{
		printf("handle_library: %s\n", library.name.c_str());
		
		traversed_libraries.insert(library.name);
		
		// recurse library dependencies
		
		for (std::string & library_dependency : library.library_dependencies)
		{
			if (traversed_libraries.count(library_dependency) != 0)
				continue;
			
			ChibiLibrary * library = s_chibiInfo.find_library(library_dependency.c_str());
			
			if (library == nullptr)
			{
				report_error(nullptr, "failed to find library dependency: %s", library_dependency.c_str());
				return false;
			}
			else
			{
				handle_library(*library, traversed_libraries, libraries);
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
			}
			
			sb.Append("\n");
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
				StringBuilder<4096> sb;
				
				sb.Append("cmake_minimum_required(VERSION 2.6)\n");
				sb.Append("\n");
				sb.Append("set(CMAKE_CXX_STANDARD 14)\n");
				sb.Append("\n");
				
				fprintf(f, "%s", sb.ToString());
			}
			
			for (auto & library : libraries)
			{
				if (library->isExecutable)
					continue;
				
				StringBuilder<4096> sb;
				
				sb.AppendFormat("# --- library %s ---\n", library->name.c_str());
				sb.Append("\n");
				sb.Append("add_library(");
				sb.Append(library->name.c_str());
				
				for (auto & file : library->files)
				{
					/*
					char absolute_path[PATH_MAX];
					if (!concat(absolute_path, sizeof(absolute_path), library->path.c_str(), "/", file.filename.c_str()))
					{
						report_error(nullptr, "failed to create absolute path");
						return false;
					}
					*/
					
					sb.Append("\n\t");
					//sb.AppendFormat("\"%s\"", absolute_path);
					sb.AppendFormat("\"%s\"", file.filename.c_str());
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
				if (!write_header_paths(sb, *library))
					return false;
				
				if (!write_compile_definitions(sb, *library))
					return false;
				
				if (!library->library_dependencies.empty())
				{
					for (auto & library_dependency : library->library_dependencies)
					{
						sb.AppendFormat("target_link_libraries(%s PRIVATE %s)\n",
							library->name.c_str(),
							library_dependency.c_str());
					}
					
					sb.Append("\n");
				}
				
				fprintf(f, "%s", sb.ToString());
			}
			
			for (auto & app : libraries)
			{
				if (app->isExecutable == false)
					continue;
				
				StringBuilder<4096> sb;
				
				sb.AppendFormat("# --- app %s ---\n", app->name.c_str());
				sb.Append("\n");
				sb.Append("add_executable(");
				sb.Append(app->name.c_str());
				
				for (auto & file : app->files)
				{
					char absolute_path[PATH_MAX];
					if (!concat(absolute_path, sizeof(absolute_path), app->path.c_str(), "/", file.filename.c_str()))
					{
						report_error(nullptr, "failed to create absolute path", app->path.c_str());
						return false;
					}
					
					sb.Append("\n\t");
					sb.Append(absolute_path);
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
				if (!write_header_paths(sb, *app))
					return false;
				
				if (!write_compile_definitions(sb, *app))
					return false;
				
				if (!app->library_dependencies.empty())
				{
					for (auto & library_dependency : app->library_dependencies)
					{
						sb.AppendFormat("target_link_libraries(%s PRIVATE %s)\n",
							app->name.c_str(),
							library_dependency.c_str());
					}
					
					sb.Append("\n");
				}
				
				fprintf(f, "%s", sb.ToString());
			}
			
			fclose(f);
		}
		
		return true;
	}
};

int main(int argc, const char * argv[])
{
	// todo : recursively find build_root
	
	char cwd[PATH_MAX];
	if (getcwd(cwd, PATH_MAX) == nullptr)
	{
		report_error(nullptr, "failed to get current working directory");
		return -1;
	}
	
	s_platform = "macos";
	
	// todo : is SDL path normalized?

	char current_path[PATH_MAX];
	strcpy_s(current_path, sizeof(current_path), cwd);

	char build_root[PATH_MAX];
	memset(build_root, 0, sizeof(build_root));

	for (;;)
	{
		strcpy_s(build_root, sizeof(build_root), current_path);
		strcat_s(build_root, sizeof(build_root), "/chibi_root");

		if (file_exist(build_root))
			break;
		else
		{
			char * term = strrchr(current_path, '/');

			if (term == nullptr)
			{
				report_error(nullptr, "failed to find chibi_root file");
				return -1;
			}
			else
				*term = 0;
		}
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
