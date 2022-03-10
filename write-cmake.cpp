#include "base64.h"
#include "chibi-internal.h"
#include "filesystem.h"
#include "plistgenerator.h"
#include "stringbuilder.h"

#include <algorithm>
#include <assert.h>
#include <deque>
#include <limits.h> // PATH_MAX
#include <set>
#include <stdarg.h>
#include <string>

#ifdef _MSC_VER
	#include <stdlib.h> // _MAX_PATH
	#ifndef PATH_MAX
		#define PATH_MAX _MAX_PATH
	#endif
#endif

#if defined(__GNUC__)
	#define sprintf_s(s, ss, f, ...) snprintf(s, ss, f, __VA_ARGS__)
	#define vsprintf_s(s, ss, f, a) vsnprintf(s, ss, f, a)
	#define strcpy_s(d, ds, s) strcpy(d, s)
	#define sscanf_s sscanf
#endif

using namespace chibi;
using namespace chibi_filesystem;

static void report_error(const char * line, const char * format, ...)
{
	char text[1024];
	va_list ap;
	va_start(ap, format);
	vsprintf_s(text, sizeof(text), format, ap);
	va_end(ap);
	
	//
	
	printf("error: %s\n", text);
}

static const char * translate_toolchain_to_cmake(const std::string & name)
{
	if (name == "msvc")
		return "MSVC";
	
	return nullptr;
}

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

static std::string always_conditional_begin;
static std::string always_conditional_end;

static std::string dont_makearchive_conditional_begin;
static std::string dont_makearchive_conditional_end;

static std::string makearchive_conditional_begin;
static std::string makearchive_conditional_end;

struct CMakeWriter
{
	bool handle_library(const ChibiInfo & chibi_info, ChibiLibrary & library, std::set<std::string> & traversed_libraries, std::vector<ChibiLibrary*> & libraries)
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
				ChibiLibrary * found_library = chibi_info.find_library(library_dependency.name.c_str());
				
				if (found_library == nullptr)
				{
					report_error(nullptr, "failed to find library dependency: %s for target %s", library_dependency.name.c_str(), library.name.c_str());
					return false;
				}
				else
				{
					if (handle_library(chibi_info, *found_library, traversed_libraries, libraries) == false)
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
	}
	
	template <typename S>
	static bool write_app_resource_paths(
		const ChibiInfo & chibi_info,
		S & sb,
		const ChibiLibrary & app,
		const std::vector<ChibiLibraryDependency> & library_dependencies)
	{
		// write a formatted list of all resource paths to CHIBI_RESOURCE_PATHS

		{
			// for debug/release builds : write the absolute paths to the app/library locations
			// this will allow for real-time editing to work, directly from the original source
			// locations
			
			StringBuilder resource_paths;

			resource_paths.Append("type,name,path\n");

			if (app.resource_path.empty() == false)
			{
				resource_paths.AppendFormat("%s,%s,%s\n",
					"app",
					app.name.c_str(),
					app.resource_path.c_str());
			}

			for (auto & library_dependency : library_dependencies)
			{
				if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
				{
					auto * library = chibi_info.find_library(library_dependency.name.c_str());
					
					if (library->resource_path.empty() == false)
					{
						resource_paths.AppendFormat("%s,%s,%s\n",
							"library",
							library->name.c_str(),
							library->resource_path.c_str());
					}
				}
			}

			const std::string resource_paths_base64 = base64_encode(
				resource_paths.text.c_str(),
				resource_paths.text.size());

			sb.AppendFormat("target_compile_definitions(%s PRIVATE %sCHIBI_RESOURCE_PATHS=\"%s\"%s)\n",
				app.name.c_str(),
				dont_makearchive_conditional_begin.c_str(),
				resource_paths_base64.c_str(),
				dont_makearchive_conditional_end.c_str());
			sb.Append("\n");
		}
		
		{
			// for distribution builds : write the relative paths to the app/library locations
			// inside the app bundle or package location. this will allow for distributing the
			// app with all files located within the same directory structure
			
			StringBuilder resource_paths;

			resource_paths.Append("type,name,path\n");

			for (auto & library_dependency : library_dependencies)
			{
				if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
				{
					auto * library = chibi_info.find_library(library_dependency.name.c_str());
					
					if (library->resource_path.empty() == false)
					{
						resource_paths.AppendFormat("%s,%s,libs/%s\n",
							"library",
							library->name.c_str(),
							library->name.c_str());
					}
				}
			}

			if (s_platform == "windows")
			{
				resource_paths.AppendFormat("%s,%s,%s\n",
					"app",
					app.name.c_str(),
					"data");
			}

			const std::string resource_paths_base64 = base64_encode(
				resource_paths.text.c_str(),
				resource_paths.text.size());

			sb.AppendFormat("target_compile_definitions(%s PRIVATE %sCHIBI_RESOURCE_PATHS=\"%s\"%s)\n",
				app.name.c_str(),
				makearchive_conditional_begin.c_str(),
				resource_paths_base64.c_str(),
				makearchive_conditional_end.c_str());
			sb.Append("\n");
		}
		
		return true;
	}
	
	template <typename S>
	static void write_custom_command_for_distribution(
		S & sb,
		const char * target,
		const char * depends,
		const char * command)
	{
		// note : command may be quite lengthy, so we avoid AppendFormat here
		sb.Append("set(args "); sb.Append(command); sb.Append(")\n");
		sb.AppendFormat(
			"add_custom_command(\n" \
				"\tTARGET %s POST_BUILD\n" \
				"\tCOMMAND %secho%s \"$<1:${args}>\"\n",
			target,
			dont_makearchive_conditional_begin.c_str(),
			dont_makearchive_conditional_end.c_str());
		if (depends != nullptr && depends[0] != 0)
			sb.AppendFormat("\tDEPENDS %s\n", depends);
		sb.Append("\tCOMMAND_EXPAND_LISTS)\n");
		sb.Append("unset(args)\n");
		sb.Append("\n");
	}
	
	template <typename S>
	static void write_custom_command_for_distribution_va(
		S & sb,
		const char * target,
		const char * depends,
		const char * command_format,
		...)
	{
		char command[1024];
		va_list ap;
		va_start(ap, command_format);
		vsprintf_s(command, sizeof(command), command_format, ap);
		va_end(ap);
		
		// note : command may be quite lengthy, so we avoid AppendFormat here
		sb.Append("set(args "); sb.Append(command); sb.Append(")\n");
		sb.AppendFormat(
			"add_custom_command(\n" \
				"\tTARGET %s POST_BUILD\n" \
				"\tCOMMAND %secho%s \"$<1:${args}>\"\n",
			target,
			dont_makearchive_conditional_begin.c_str(),
			dont_makearchive_conditional_end.c_str());
		if (depends != nullptr && depends[0] != 0)
			sb.AppendFormat("\tDEPENDS %s\n", depends);
		sb.Append("\tCOMMAND_EXPAND_LISTS)\n");
		sb.Append("unset(args)\n");
		sb.Append("\n");
	}
	
	template <typename S>
	static bool write_copy_resources_for_distribution_using_rsync(S & sb, const ChibiLibrary & app, const ChibiLibrary & library, const char * destination_path)
	{
		// use rsync to copy resources

		// but first make sure the target directory exists

		// note : we use a conditional to check if we're building a distribution app bundle
		//        ideally CMake would have build config dependent custom commands,
		//        but since it doesn't, we prepend 'echo' to the command, depending on
		//        whether this is a distribution build or not

		write_custom_command_for_distribution_va(sb,
			app.name.c_str(),
			library.resource_path.c_str(),
			"${CMAKE_COMMAND} -E make_directory \"%s\"",
			destination_path);
		
		StringBuilder exclude_args;

		if (library.resource_excludes.empty() == false)
		{
			for (auto & exclude : library.resource_excludes)
			{
				sb.Append("--exclude '");
				sb.Append(exclude.c_str());
				sb.Append("' ");
			}
		}

		// rsync
		write_custom_command_for_distribution_va(sb,
			app.name.c_str(),
			library.resource_path.c_str(),
			"rsync -a %s \"%s/\" \"%s\"",
			exclude_args.text.c_str(),
			library.resource_path.c_str(),
			destination_path);
		
		return true;
	}
	
	template <typename S>
	static bool write_copy_license_files_for_distribution_using_rsync(
		S & sb,
		const ChibiLibrary & app,
		const ChibiLibrary & library,
		const char * destination_path)
	{
		assert(!library.license_files.empty());
		
		// use rsync to copy the license file(s)

		// but first make sure the target directory exists

		// note : we use a conditional to check if we're building a distribution app bundle
		//        ideally CMake would have build config dependent custom commands,
		//        but since it doesn't, we prepend 'echo' to the command, depending on
		//        whether this is a distribution build or not
		
		StringBuilder command;
		command.AppendFormat(
			"${CMAKE_COMMAND} -E make_directory \"%s\"",
			destination_path);
		
		write_custom_command_for_distribution(sb,
			app.name.c_str(),
			nullptr,
			command.text.c_str());
		
		command.Reset();
		command.Append("rsync\n");
		for (auto & license_file : library.license_files)
		{
			char full_path[PATH_MAX];
			if (!concat(full_path, sizeof(full_path), library.path.c_str(), "/", license_file.c_str()))
			{
				report_error(nullptr, "failed to create absolute path");
				return false;
			}
			
			command.AppendFormat("\t\"%s\"\n", full_path);
		}
		command.AppendFormat("\t\"%s\"", destination_path);
		
		write_custom_command_for_distribution(sb,
			app.name.c_str(),
			destination_path,
			command.text.c_str());
	
		return true;
	}

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
					sb.Append("\t");
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
						
						strcpy_s(condition_end, sizeof(condition_end), ">");
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
	
	static bool gather_all_library_dependencies(const ChibiInfo & chibi_info, const ChibiLibrary & library, std::vector<ChibiLibraryDependency> & library_dependencies)
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
						const ChibiLibrary * resolved_library = chibi_info.find_library(library_dependency.name.c_str());
						
						if (resolved_library == nullptr)
						{
							report_error(nullptr, "failed to resolve library dependency: %s for library %s", library_dependency.name.c_str(), library->name.c_str());
							return false;
						}

						stack.push_back(resolved_library);
					}
				}
			}
			
			stack.pop_front();
		}
		
		return true;
	}
	
	static bool write_embedded_app_files(
		const ChibiInfo & chibi_info,
		StringBuilder & sb,
		const ChibiLibrary & app,
		const std::vector<ChibiLibraryDependency> & library_dependencies)
	{
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
				
				if (s_platform == "macos" || s_platform == "iphoneos")
				{
					if (string_ends_with(filename, ".framework"))
					{
						// use rsync to recursively copy files if this is a framework
						
						// but first make sure the target directory exists
						
						sb.AppendFormat("set(args ${CMAKE_COMMAND} -E make_directory \"${BUNDLE_PATH}/Contents/Frameworks\")\n");
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND %secho%s \"$<1:${args}>\"\n" \
								"\tCOMMAND_EXPAND_LISTS\n" \
								"\tDEPENDS \"%s\")\n",
							app.name.c_str(),
							always_conditional_begin.c_str(),
							always_conditional_end.c_str(),
							library_dependency.path.c_str());
						
						// rsync
						sb.AppendFormat("set(args rsync -a \"%s\" \"${BUNDLE_PATH}/Contents/Frameworks\")\n",
							library_dependency.path.c_str());
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND %secho%s \"$<1:${args}>\"\n" \
								"\tCOMMAND_EXPAND_LISTS\n" \
								"\tDEPENDS \"%s\")\n",
							app.name.c_str(),
							always_conditional_begin.c_str(),
							always_conditional_end.c_str(),
							library_dependency.path.c_str());
					}
					else
					{
						// just copy the file (if it has changed or doesn't exist)
						
						sb.AppendFormat("set(args ${CMAKE_COMMAND} -E copy_if_different \"%s\" \"${BUNDLE_PATH}/Contents/MacOS/%s\")\n",
							library_dependency.path.c_str(),
							filename);
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND %secho%s \"$<1:${args}>\"\n" \
								"\tCOMMAND_EXPAND_LISTS\n" \
								"\tDEPENDS \"%s\")\n",
							app.name.c_str(),
							always_conditional_begin.c_str(),
							always_conditional_end.c_str(),
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
				const ChibiLibrary * library = chibi_info.find_library(library_dependency.name.c_str());
				
				for (auto & dist_file : library->dist_files)
				{
					// create a custom command where the generated file(s) are copied into a place where the executable can find it
					
					const char * filename;
				
					auto i = dist_file.find_last_of('/');
				
					if (i == std::string::npos)
						filename = dist_file.c_str();
					else
						filename = &dist_file[i + 1];
					
					if (s_platform == "macos")
					{
						sb.AppendFormat(
							"add_custom_command(\n" \
								"\tTARGET %s POST_BUILD\n" \
								"\tCOMMAND ${CMAKE_COMMAND} -E copy_if_different \"%s\" \"${BUNDLE_PATH}/Contents/MacOS/%s\"\n" \
								"\tDEPENDS \"%s\")\n",
							app.name.c_str(),
							dist_file.c_str(),
							filename,
							dist_file.c_str());
					}
					else
					{
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
				
				if (library->shared)
				{
					// copy generated shared object files into a place where the executable can find it
					
					if (s_platform == "macos")
					{
						write_custom_command_for_distribution_va(sb,
							app.name.c_str(),
							library_dependency.name.c_str(),
							"${CMAKE_COMMAND} -E copy_if_different\n" \
							"\t\"$<TARGET_FILE:%s>\"\n" \
							"\t\"${BUNDLE_PATH}/Contents/MacOS/$<TARGET_FILE_NAME:%s>\"",
							library_dependency.name.c_str(),
							library_dependency.name.c_str());
					}
					else if (s_platform == "iphoneos")
					{
						write_custom_command_for_distribution_va(sb,
							app.name.c_str(),
							library_dependency.name.c_str(),
							"${CMAKE_COMMAND} -E copy_if_different\n" \
							"\t\"$<TARGET_FILE:%s>\"\n" \
							"\t\"${BUNDLE_PATH}/$<TARGET_FILE_NAME:%s>\"",
							library_dependency.name.c_str(),
							library_dependency.name.c_str());
					}
					
					// todo : also copy generated (dll) files on Windows (?)
				}
			}
		}
		
		return true;
	}

	static bool write_create_windows_app_archive(const ChibiInfo & chibi_info, StringBuilder & sb, const ChibiLibrary & app, const std::vector<ChibiLibraryDependency> & library_dependencies)
	{
		// create a directory where to copy the executable, distribution and data files
		
		write_custom_command_for_distribution_va(sb,
			app.name.c_str(),
			nullptr,
			"${CMAKE_COMMAND} -E make_directory \"${CMAKE_CURRENT_BINARY_DIR}/%s\"",
			app.name.c_str());

		// copy the generated executable

		write_custom_command_for_distribution_va(sb,
			app.name.c_str(),
			nullptr,
			"${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:%s> \"${CMAKE_CURRENT_BINARY_DIR}/%s\"",
			app.name.c_str(),
			app.name.c_str());

		for (auto & library_dependency : library_dependencies)
		{
			if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
			{
				const ChibiLibrary * library = chibi_info.find_library(library_dependency.name.c_str());
				
				// copy generated DLL files

				if (library->shared)
				{
					write_custom_command_for_distribution_va(sb,
						app.name.c_str(),
						"\"$<TARGET_FILE:%s>\"",
						"${CMAKE_COMMAND} -E copy_if_different \"$<TARGET_FILE:%s>\" \"${CMAKE_CURRENT_BINARY_DIR}/%s\"",
						library_dependency.name.c_str(),
						app.name.c_str());
				}

				// copy the distribution files

				for (auto & dist_file : library->dist_files)
				{
					// create a custom command where the embedded file(s) are copied into a place where the executable can find it
					
					write_custom_command_for_distribution_va(sb,
						app.name.c_str(),
						nullptr,
						"${CMAKE_COMMAND} -E copy_if_different \"%s\" \"${CMAKE_CURRENT_BINARY_DIR}/%s\"",
						dist_file.c_str(),
						app.name.c_str());
				}
			}
		}

		// copy app resources

		if (app.resource_path.empty() == false)
		{
			write_custom_command_for_distribution_va(sb,
				app.name.c_str(),
				nullptr,
				"${CMAKE_COMMAND} -E make_directory \"${CMAKE_CURRENT_BINARY_DIR}/%s/data\"",
				app.name.c_str());

			write_custom_command_for_distribution_va(sb,
				app.name.c_str(),
				nullptr,
				"${CMAKE_COMMAND} -E copy_directory \"%s\" \"${CMAKE_CURRENT_BINARY_DIR}/%s/data\"",
				app.resource_path.c_str(),
				app.name.c_str());
		}

		// copy library resources
		
		for (auto & library_dependency : library_dependencies)
		{
			if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
			{
				auto * library = chibi_info.find_library(library_dependency.name.c_str());
				
				if (library->resource_path.empty() == false)
				{
					write_custom_command_for_distribution_va(sb,
						app.name.c_str(),
						nullptr,
						"${CMAKE_COMMAND} -E make_directory \"${CMAKE_CURRENT_BINARY_DIR}/%s/data/libs/%s\"",
						app.name.c_str(),
						library->name.c_str());

					write_custom_command_for_distribution_va(sb,
						app.name.c_str(),
						nullptr,
						"${CMAKE_COMMAND} -E copy_directory \"%s\" \"${CMAKE_CURRENT_BINARY_DIR}/%s/data/libs/%s\"",
						library->resource_path.c_str(),
						app.name.c_str(),
						library->name.c_str());
				}
			}
		}

		sb.Append("\n");

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
	
	static void write_set_osx_bundle_path(StringBuilder & sb, const char * app_name)
	{
		sb.AppendFormat("set(BUNDLE_PATH \"$<TARGET_FILE_DIR:%s>/../..\")\n\n", app_name);
	}

	static void write_set_ios_bundle_path(StringBuilder & sb, const char * app_name)
	{
		sb.AppendFormat("set(BUNDLE_PATH \"\\$\\{CONFIGURATION_BUILD_DIR\\}/\\$\\{CONTENTS_FOLDER_PATH\\}\")\n\n", app_name);
	}

	static bool generate_translation_unit_linkage_files(const ChibiInfo & chibi_info, StringBuilder & sb, const char * generated_path, const std::vector<ChibiLibrary*> & libraries)
	{
		// generate translation unit linkage files

		for (auto * app : libraries)
		{
			if (app->isExecutable == false)
				continue;

			std::vector<ChibiLibraryDependency> all_library_dependencies;
			if (!gather_all_library_dependencies(chibi_info, *app, all_library_dependencies))
				return false;

			std::vector<std::string> link_translation_unit_using_function_calls;
			
			for (auto & library_dependency : all_library_dependencies)
			{
				if (library_dependency.type != ChibiLibraryDependency::kType_Generated)
					continue;

				auto * library = chibi_info.find_library(library_dependency.name.c_str());

				link_translation_unit_using_function_calls.insert(
					link_translation_unit_using_function_calls.end(),
					library->link_translation_unit_using_function_calls.begin(),
					library->link_translation_unit_using_function_calls.end());
			}

			if (link_translation_unit_using_function_calls.empty() == false)
			{
				// generate translation unit linkage file
				
				StringBuilder text_sb;
			
				text_sb.Append("// auto-generated. do not hand-edit\n\n");

				for (auto & function_name : link_translation_unit_using_function_calls)
					text_sb.AppendFormat("extern void %s();\n", function_name.c_str());
				text_sb.Append("\n");

				text_sb.Append("void linkTranslationUnits()\n");
				text_sb.Append("{\n");
				{
					for (auto & function_name : link_translation_unit_using_function_calls)
						text_sb.AppendFormat("\t%s();\n", function_name.c_str());
				}
				text_sb.Append("}\n");

				char full_path[PATH_MAX];
				if (!concat(full_path, sizeof(full_path), generated_path, "/translation_unit_linkage-", app->name.c_str(), ".cpp"))
				{
					report_error(nullptr, "failed to create absolute path");
					return false;
				}

				if (!write_text_to_file_if_contents_changed(sb, text_sb.text.c_str(), full_path))
				{
					report_error(nullptr, "failed to write translation unit linkage file. path: %s", full_path);
					return false;
				}
				
				// add the translation unit linkage file to the list of app files
				
				ChibiLibraryFile file;
				file.filename = full_path;

				app->files.push_back(file);
			}
		}

		return true;
	}

	static bool write_text_to_file_if_contents_changed(StringBuilder & sb, const char * text, const char * filename)
	{
		// escape the text so we can use 'file(WRITE ..)' from cmake to create the actual file inside the right directory
		
		std::string escaped_text;
		
		for (int i = 0; text[i] != 0; ++i)
		{
			const char c = text[i];

			if (c == '"')
			{
				escaped_text.push_back('\\');
				escaped_text.push_back('"');
			}
			else
				escaped_text.push_back(c);
		}

		sb.AppendFormat("file(WRITE \"%s.txt\" \"%s\")\n", filename, escaped_text.c_str());
		sb.AppendFormat("file(GENERATE OUTPUT \"%s\" INPUT \"%s.txt\")\n", filename, filename);
		sb.Append("\n");

		return true;
	}
	
	bool write(const ChibiInfo & chibi_info, const char * platform, const char * output_filename)
	{
		// decode platform
		
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
		
		// gather the library targets to emit
		
		std::set<std::string> traversed_libraries;
		
		std::vector<ChibiLibrary*> libraries;
		
		for (auto & library : chibi_info.libraries)
		{
			if (traversed_libraries.count(library->name) != 0)
				continue;
			
			if (library->isExecutable && chibi_info.should_build_target(library->name.c_str()))
			{
				if (handle_library(chibi_info, *library, traversed_libraries, libraries) == false)
					return false;
			}
		}
		
		for (auto & library : chibi_info.libraries)
		{
			if (traversed_libraries.count(library->name) != 0)
				continue;
			
			if (chibi_info.should_build_target(library->name.c_str()))
			{
				if (handle_library(chibi_info, *library, traversed_libraries, libraries) == false)
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
		
	// todo : make conglomerate generation independent of cmake writer

		// generate conglomerate files
		
		for (auto * library : libraries)
		{
			// build a set of conglomerate files and the files which belong to them
			
			std::map<std::string, std::vector<ChibiLibraryFile*>> files_by_conglomerate;
			
			for (auto & library_file : library->files)
			{
				if (library_file.conglomerate_filename.empty())
					continue;
				
				auto & files = files_by_conglomerate[library_file.conglomerate_filename];
				
				files.push_back(&library_file);
			}
			
			// generate conglomerate files
			
			std::vector<ChibiLibraryFile> filesToAdd;
			
			for (auto & files_by_conglomerate_itr : files_by_conglomerate)
			{
				auto & conglomerate_filename = files_by_conglomerate_itr.first;
				auto & library_files = files_by_conglomerate_itr.second;
				
				StringBuilder sb;
			
				sb.Append("// auto-generated. do not hand-edit\n\n");

				for (auto * library_file : library_files)
				{
					assert(library_file->compile == false);
					
					sb.AppendFormat("#include \"%s\"\n", library_file->filename.c_str());
				}

				if (!write_if_different(sb.text.c_str(), conglomerate_filename.c_str()))
				{
					report_error(nullptr, "failed to write conglomerate file. path: %s", conglomerate_filename.c_str());
					return false;
				}
				
				// add the conglomerate file to the list of library files
				
				ChibiLibraryFile file;
				
				file.filename = conglomerate_filename;
				
				if (library->conglomerate_groups.count(conglomerate_filename) != 0)
					file.group = library->conglomerate_groups[conglomerate_filename];
	
				filesToAdd.push_back(file);
			}
			
			for (auto & file : filesToAdd)
			{
				library->files.push_back(file);
			}
		}

		// turn shared libraries into non-shared for iphoneos, since I didn't manage
		// to do code signing propertly yet, and iphoneos refuses to load our .dylibs
		if (s_platform == "iphoneos")
		{
		// todo : run code signing on generated shared libraries for macos/iphoneos
			for (auto & library : libraries)
				library->shared = false;
		}
		
		// always build a self-contained archive for iphoneos, as any build type
		// could be deployed on an actual device, and the app won't have access
		// to the local filesystem for loading resources and libraries
		if (s_platform == "iphoneos")
		{
			dont_makearchive_conditional_begin = "$<$<BOOL:false>:";
			dont_makearchive_conditional_end = ">";

			makearchive_conditional_begin = "$<$<BOOL:true>:";
			makearchive_conditional_end = ">";
		}
		else
		{
			dont_makearchive_conditional_begin = "$<$<NOT:$<CONFIG:Distribution>>:";
			dont_makearchive_conditional_end = ">";

			makearchive_conditional_begin = "$<$<CONFIG:Distribution>:";
			makearchive_conditional_end = ">";
		}
		
		always_conditional_begin = "$<$<BOOL:false>:";
		always_conditional_end = ">";
		
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
				
				sb.Append("# auto-generated. do not hand-edit\n\n");
				
				if (s_platform == "macos")
				{
					// cmake 3.8 requirement: need COMMAND_EXPAND_LISTS to work for conditional custom build steps
					sb.Append("cmake_minimum_required(VERSION 3.8)\n");
					sb.Append("\n");
				}
				else
				{
					// note : cmake 3.7 is the current version installed on Raspbian
					sb.Append("cmake_minimum_required(VERSION 3.7)\n");
					sb.Append("\n");
				}
				
				sb.Append("project(Project)\n");
				sb.Append("\n");

				sb.Append("set(CMAKE_CXX_STANDARD 11)\n");
				sb.Append("\n");
				
				sb.Append("set_property(GLOBAL PROPERTY USE_FOLDERS ON)\n");
				sb.Append("\n");

				// this translates to -fPIC on linux, which is a requirement to build share libraries
				// on macos this is a default option and always set
				sb.Append("set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n");
				sb.Append("\n");
				
				if (!chibi_info.cmake_module_paths.empty())
				{
					for (auto & cmake_module_path : chibi_info.cmake_module_paths)
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
				sb.Append("if (UNIX)\n");
				sb.Append("\tfind_package(PkgConfig REQUIRED)\n");
				sb.Append("\tif (NOT PkgConfig_FOUND)\n");
				sb.Append("\t\tmessage(FATAL_ERROR \"PkgConfig not found\")\n");
				sb.Append("\tendif (NOT PkgConfig_FOUND)\n");
				sb.Append("endif (UNIX)\n");
				sb.Append("\n");
				
				sb.Append("set(CMAKE_MACOSX_RPATH ON)\n");
				sb.Append("\n");
			// fixme : this should be defined through the user's workspace.. or plist?
				sb.Append("set(CMAKE_OSX_DEPLOYMENT_TARGET 10.11)\n");
				sb.Append("\n");

				sb.Append("if ((CMAKE_CXX_COMPILER_ID MATCHES \"MSVC\") AND NOT CMAKE_CL_64)\n");
//				sb.Append("\tadd_compile_options(/arch:AVX2)\n");
				sb.Append("\tadd_definitions(-D__SSE2__)\n"); // MSVC doesn't define __SSE__, __SSE2__ and the likes. although it _does_ define __AVX__ and __AVX2__
				sb.Append("\tadd_definitions(-D__SSSE3__)\n");
				sb.Append("endif ()\n");
				sb.Append("\n");

			// fixme : this should be defined through the user's workspace
				if (s_platform == "macos")
				{
					//sb.Append("add_compile_options(-mavx2)\n");
				}
				
				if (s_platform == "windows")
				{
					// Windows.h defines min and max macros, which are always causing issues in portable code
					// we can get rid of them by defining NOMINMAX before including Windows.h
					sb.Append("add_definitions(-DNOMINMAX)\n");
					sb.Append("\n");
				}

				// let CMake generate export definitions for all symbols it finds inside the generated object files, to normalize the behavior
				// across Windows and Linux/OSX; which, Windows being the odd one out, both do by default
			// todo : is this still needed ? we already have shared library support and the translation unit linkage feature to help us out
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
							
							// note : file(COPY.. ) will check timestamps and avoid the copy if source and destination have matching timestamps
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

			{
				// generate translation unit linkage files

				StringBuilder sb;
				
				sb.AppendFormat("# --- translation unit linkage files ---\n");
				sb.Append("\n");

				if (generate_translation_unit_linkage_files(chibi_info, sb, generated_path, libraries) == false)
					return false;

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
				
				if (library->prebuilt)
					sb.Append(" IMPORTED");
				
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
				
				if (library->prebuilt)
				{
					// special case: set imported library location
					sb.Append("# (this library import is auto-generated from a embed_framework local library dependency)\n");
					sb.Append("set_target_properties("); sb.Append(library->name.c_str());
					sb.Append("\n\tPROPERTIES IMPORTED_LOCATION");
					sb.Append("\n\t"); sb.Append(library->path.c_str());
					sb.Append(")\n");
					sb.Append("\n");
				}
				
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
				
				if (library->objc_arc)
				{
					// note : we only support enabling ARC for Apple platforms right now
					if (s_platform == "macos" || s_platform == "iphoneos")
					{
						sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" -fobjc-arc\")", library->name.c_str());
					}
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
				
				if (is_platform("android"))
				{
					// note : on android there are no real standalone executables,
					//        only shared libraries, loaded by a Java-defined activity
					sb.Append("add_library(");
					sb.Append(app->name.c_str());
					sb.Append("\n\tSHARED");
				}
				else
				{
					sb.Append("add_executable(");
					sb.Append(app->name.c_str());
					sb.Append("\n\tMACOSX_BUNDLE");
				}
				
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
					sb.Append("\n");
				}

				// copy app resources
				
				if (s_platform == "macos")
				{
					write_set_osx_bundle_path(sb, app->name.c_str());
				}
				else if (s_platform == "iphoneos")
				{
					write_set_ios_bundle_path(sb, app->name.c_str());
				}
				
				if (!app->resource_path.empty())
				{
					if (s_platform == "macos")
					{
						const char * resource_path = "${BUNDLE_PATH}/Contents/Resources";
						
						if (!write_copy_resources_for_distribution_using_rsync(sb, *app, *app, resource_path))
							return false;
						
						sb.AppendFormat("target_compile_definitions(%s PRIVATE %sCHIBI_RESOURCE_PATH=\"%s\"%s)\n",
							app->name.c_str(),
							dont_makearchive_conditional_begin.c_str(),
							app->resource_path.c_str(),
							dont_makearchive_conditional_end.c_str());
						sb.Append("\n");
					}
					else if (s_platform == "iphoneos")
					{
						const char * resource_path = "${BUNDLE_PATH}";
						
						if (!write_copy_resources_for_distribution_using_rsync(sb, *app, *app, resource_path))
							return false;
						
						// note : for iphoneos we always copy resources (so a build can always be run and debugged on
						//        a device). this means the main resource path will just be '.'
						sb.AppendFormat("target_compile_definitions(%s PRIVATE CHIBI_RESOURCE_PATH=\".\")\n",
							app->name.c_str());
						sb.Append("\n");
					}
					else
					{
						sb.AppendFormat("target_compile_definitions(%s PRIVATE %sCHIBI_RESOURCE_PATH=\"%s\"%s)\n",
							app->name.c_str(),
							dont_makearchive_conditional_begin.c_str(),
							app->resource_path.c_str(),
							dont_makearchive_conditional_end.c_str());
						sb.Append("\n");
					}
				}
				
				// copy library resources
				
				std::vector<ChibiLibraryDependency> all_library_dependencies;
				if (!gather_all_library_dependencies(chibi_info, *app, all_library_dependencies))
					return false;
				
				for (auto & library_dependency : all_library_dependencies)
				{
					if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
					{
						auto * library = chibi_info.find_library(library_dependency.name.c_str());
						
						if (library->resource_path.empty() == false)
						{
							const char * resource_path = nullptr;
							
							if (s_platform == "macos")
								resource_path = "${BUNDLE_PATH}/Contents/Resources/libs";
							else if (s_platform == "iphoneos")
								resource_path = "${BUNDLE_PATH}/libs";
							else if (s_platform == "windows")
								continue; // note : windows is handled separately in write_create_windows_app_archive
							else
								continue; // todo : add linux here
							
							assert(resource_path != nullptr);
							if (resource_path != nullptr)
							{
								char destination_path[PATH_MAX];
								concat(destination_path, sizeof(destination_path), resource_path, "/", library->name.c_str());
								if (!write_copy_resources_for_distribution_using_rsync(sb, *app, *library, destination_path))
									return false;
							}
						}
					}
				}
				
				// copy library license files
				
				for (auto & library_dependency : all_library_dependencies)
				{
					if (library_dependency.type == ChibiLibraryDependency::kType_Generated)
					{
						auto * library = chibi_info.find_library(library_dependency.name.c_str());
					
						if (library->license_files.empty() == false)
						{
							const char * license_path = nullptr;
							
							if (s_platform == "macos")
								license_path = "${BUNDLE_PATH}/Contents/license";
							else if (s_platform == "iphoneos")
								license_path = "${BUNDLE_PATH}/license";
							else
								continue; // todo : add windows and linux here
							
							assert(license_path != nullptr);
							if (license_path != nullptr)
							{
								char destination_path[PATH_MAX];
								concat(destination_path, sizeof(destination_path), license_path, "/", library->name.c_str());
								
								if (!write_copy_license_files_for_distribution_using_rsync(sb, *app, *library, destination_path))
									return false;
							}
						}
					}
				}
				
				if (!write_app_resource_paths(chibi_info, sb, *app, all_library_dependencies))
					return false;
				
				if (!write_header_paths(sb, *app))
					return false;
				
				if (!write_compile_definitions(sb, *app))
					return false;
				
				if (!write_library_dependencies(sb, *app))
					return false;
				
				if (!write_package_dependencies(sb, *app))
					return false;
				
				if (!write_embedded_app_files(chibi_info, sb, *app, all_library_dependencies))
					return false;
				
				if (s_platform == "windows")
				{
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY LINK_FLAGS \" /SAFESEH:NO\")\n", app->name.c_str());
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" /wd4244\")\n", app->name.c_str()); // disable 'conversion from type A to B, possible loss of data' warning
					sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" /wd4018\")\n", app->name.c_str()); // disable 'signed/unsigned mismatch' warning
					
					sb.Append("\n");
				}
				
				if (app->objc_arc)
				{
					// note : we only support enabling ARC for Apple platforms right now
					if (s_platform == "macos" || s_platform == "iphoneos")
					{
						sb.AppendFormat("set_property(TARGET %s APPEND_STRING PROPERTY COMPILE_FLAGS \" -fobjc-arc\")", app->name.c_str());
					}
				}
				
				if (s_platform == "macos" || s_platform == "iphoneos")
				{
					// note : APPLE_GUI_IDENTIFIER must be set before generate_plist
					// todo : imagine a clean way to set the identifier
					sb.AppendFormat("set(APPLE_GUI_IDENTIFIER \"com.chibi.%s\")\n",
						app->name.c_str());
				}
				
				if (s_platform == "macos" || s_platform == "iphoneos")
				{
					// generate plist text
					
					std::string text;
					
				// todo : let these plist flags originate from apps/libraries that need them
				// todo : add opportunity to merge with custom plist files, for advanced plist settings
					if (!generate_plist(nullptr, app->name.c_str(),
						kPlistFlag_HighDpi |
						kPlistFlag_AccessWebcam |
						kPlistFlag_AccessMicrophone,
						text))
					{
						report_error(nullptr, "failed to generate plist file");
						return false;
					}
					
					// write the plist file to disk
					
					char plist_path[PATH_MAX];
					if (!concat(plist_path, sizeof(plist_path), generated_path, "/", app->name.c_str(), ".plist"))
					{
						report_error(nullptr, "failed to create plist path");
						return false;
					}

					if (!write_text_to_file_if_contents_changed(sb, text.c_str(), plist_path))
						return false;

					// tell cmake to use our generated plist file
					
					sb.AppendFormat("set_target_properties(%s PROPERTIES MACOSX_BUNDLE_INFO_PLIST \"%s\")\n",
						app->name.c_str(),
						plist_path);
					sb.Append("\n");
				}

				if (s_platform == "macos")
				{
					// add rpath to the generated executable so that it can find dylibs inside the location of the executable itself. this is needed when copying generated shared libraries into the app bundle
					
					// note : we use a conditional to check if we're building a distribution app bundle
					//        ideally CMake would have build config dependent custom commands,
					//        but since it doesn't, we prepend 'echo' to the command, depending on
					//        whether this is a distribution build or not
				
				// fixme : cmake is broken and always runs the custom command, regardless of whether the DEPENDS target is dirty or not. this causes install_name_tool to fail, as the rpath has already been set. I've appended "|| true" at the end of the command, to effectively ignore the return code from install_name_tool. a nasty side effect of this is we don't know whether the command succeeded or actually failed for some valid reason.. so ideally this hack is removed once cmake's behavior is fixed
				
					write_custom_command_for_distribution_va(sb,
						app->name.c_str(),
						app->name.c_str(),
						"install_name_tool -add_rpath \"@executable_path\" \"${BUNDLE_PATH}/Contents/MacOS/%s\" || true",
						app->name.c_str());
				}
				
				if (s_platform == "windows")
				{
					write_create_windows_app_archive(chibi_info, sb, *app, all_library_dependencies);
				}
			
				if (s_platform == "macos" || s_platform == "iphoneos")
				{
					// unset bundle path when we're done processing this app
					sb.Append("unset(BUNDLE_PATH)\n\n");
				}
				
				if (s_platform == "macos" || s_platform == "iphoneos")
				{
					// unset apple app identifier when we're done processing this app
					sb.Append("unset(APPLE_GUI_IDENTIFIER)");
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

namespace chibi
{
	bool write_cmake_file(const ChibiInfo & chibi_info, const char * platform, const char * output_filename)
	{
		CMakeWriter writer;
		
		return writer.write(chibi_info, platform, output_filename);
	}
}
