#include "Debugging.h"
#include "filesystem.h"
#include "Path.h"
#include "StringBuilder.h"

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

static void report_error(const char * format, ...)
{
	char text[1024];
	va_list ap;
	va_start(ap, format);
	vsprintf(text, format, ap);
	va_end(ap);
	
	printf("error: %s", text);
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
		report_error("invalid path");
		return false;
	}

	*term = 0;
	
	return true;
}

struct ChibiLibraryFile
{
	std::string filename;
};

struct ChibiLibrary
{
	std::string name;
	std::string path;
	
	bool isExecutable = false;
	
	std::vector<ChibiLibraryFile> files;
	
	std::vector<std::string> library_dependencies;
	
	std::vector<std::string> package_dependencies;
	
	std::vector<std::string> header_paths;
	
	void dump_info() const
	{
		printf("library: %s\n", name.c_str());
		
		for (auto & file : files)
		{
			printf("\tfile: %s\n", file.filename.c_str());
		}
		
		for (auto & header_path : header_paths)
		{
			printf("\theader path: %s\n", header_path.c_str());
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

static bool process_chibi_file(const char * filename)
{
	char chibi_path[PATH_MAX];

	if (!get_path_from_filename(filename, chibi_path, PATH_MAX))
	{
		report_error("failed to get path from chibi filename");
		return false;
	}
	
	//
	
	FILE * f = fopen(filename, "rt");

	if (f == nullptr)
	{
		report_error("failed to open %s", filename);
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
				
				if (eat_word(linePtr, "add"))
				{
					char * location;
					
					if (!eat_word_v2(linePtr, location))
					{
						report_error("missing location: %s\n", line);
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
						report_error("missing name");
						return false;
					}
						
					if (s_chibiInfo.library_exists(name))
					{
						report_error("library already exists");
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
						report_error("missing name");
						return false;
					}
					
					if (s_chibiInfo.library_exists(name))
					{
						report_error("app already exists");
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
						report_error("add_files without a target");
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
						report_error("scan_files without a target");
						return false;
					}
					else
					{
						char * extension;
						
						if (!eat_word_v2(linePtr, extension))
						{
							report_error("missing extension");
							return false;
						}
						
						bool traverse = false;
						
						for (;;)
						{
							char * option;
							
							if (eat_word_v2(linePtr, option) == false)
								break;
							
							if (!strcmp(option, "traverse"))
							{
								printf("traverse!\n");
								traverse = true;
							}
						}
						
						// scan files
						
						auto filenames = listFiles(chibi_path, traverse);
						
						for (auto & filename : filenames)
						{
							if (Path::GetExtension(filename, true) != extension)
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
						report_error("exclude_files without a target");
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
								report_error("failed to create absolute path");
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
						report_error("depend_package without a target");
						return false;
					}
					else
					{
						char * name;
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error("missing name");
							return false;
						}
						
						s_currentLibrary->package_dependencies.push_back(name);
					}
				}
				else if (eat_word(linePtr, "depend_library"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error("depend_library without a target");
						return false;
					}
					else
					{
						char * name;
						
						if (!eat_word_v2(linePtr, name))
						{
							report_error("missing name");
							return false;
						}
						
						s_currentLibrary->library_dependencies.push_back(name);
					}
				}
				else if (eat_word(linePtr, "header_path"))
				{
					if (s_currentLibrary == nullptr)
					{
						report_error("header_path without a target");
						return false;
					}
					else
					{
						char * path;
						
						if (!eat_word_v2(linePtr, path))
						{
							report_error("missing path");
							return false;
						}
						
						bool expose = false;
						
						for (;;)
						{
							char * option;
							
							if (!eat_word_v2(linePtr, option))
								break;
							
							if (!strcmp(option, "expose"))
								expose = true;
							else
							{
								report_error("unknown option: %s", option);
								return false;
							}
						}
						
						char full_path[PATH_MAX];
						if (!concat(full_path, sizeof(full_path), chibi_path, "/", path))
						{
							report_error("failed to create absolute path");
							return false;
						}
						
						s_currentLibrary->header_paths.push_back(full_path);
					}
				}
				else if (eat_word(linePtr, "resource_path"))
				{
					if (s_currentLibrary == nullptr || s_currentLibrary->isExecutable == false)
					{
						report_error("resource_path without a target");
						return false;
					}
					else
					{
						char * path;
						
						if (!eat_word_v2(linePtr, path))
						{
							report_error("missing path");
							return false;
						}
					}
				}
				else
				{
					report_error("syntax error: %s", line);
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
				report_error("failed to find library dependency: %s", library_dependency.c_str());
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
		FILE * f = fopen("/Users/thecat/chibi/output/CMakeLists.txt", "wt");
		
		if (f == nullptr)
		{
			report_error("failed to open output file");
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
						report_error("failed to create absolute path");
						return false;
					}
					*/
					
					sb.Append("\n\t");
					//sb.AppendFormat("\"%s\"", absolute_path);
					sb.AppendFormat("\"%s\"", file.filename.c_str());
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
				if (!library->header_paths.empty())
				{
					for (auto & header_path : library->header_paths)
					{
						const char * visibility = "PUBLIC"; // todo : visibility
						
						sb.AppendFormat("target_include_directories(%s %s \"%s\")\n",
							library->name.c_str(),
							visibility,
							header_path.c_str());
					}
					
					sb.Append("\n");
				}
				
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
						report_error("failed to create absolute path");
						return false;
					}
					
					sb.Append("\n\t");
					sb.Append(absolute_path);
				}
				
				sb.Append(")\n");
				sb.Append("\n");
				
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
		report_error("failed to get current working directory");
		return -1;
	}

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
				report_error("failed to find chibi_root file");
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
		report_error("an error occured while scanning for chibi files. abort");
		return -1;
	}
	
	s_chibiInfo.dump_info();
	
	CMakeWriter writer;
	
	writer.write();

	return 0;
}
