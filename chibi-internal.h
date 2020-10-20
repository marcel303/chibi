#pragma

#include <map>
#include <set>
#include <string>
#include <vector>

#include "stringhelpers.h" // todo : move to cpp file
using namespace chibi; // todo : move to cpp file

#define ENABLE_PKGCONFIG 0 // todo : pkgconfig shouldn't be used in chibi.txt files. but it would be nice to define libraries using pkgconfig externally, as a sort of aliases, which can be used in a normalized fashion as a regular library

struct ChibiLibraryFile
{
	std::string filename;
	
	std::string group;
	
	std::string conglomerate_filename;
	
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
	
	bool shared = false; // generate a shared library
	bool prebuilt = false; // library contains prebuilt files instead of source files
	
	bool isExecutable = false;
	
	std::vector<ChibiLibraryFile> files;
	
	std::vector<ChibiLibraryDependency> library_dependencies;
	
	std::vector<ChibiPackageDependency> package_dependencies;
	
	std::vector<ChibiHeaderPath> header_paths;
	
	std::vector<ChibiCompileDefinition> compile_definitions;
	
	std::map<std::string, std::string> conglomerate_groups; // best-guess group names for conglomerate files
	
	std::string resource_path;
	std::vector<std::string> resource_excludes;

	std::vector<std::string> dist_files;

	std::vector<std::string> license_files;

	std::vector<std::string> link_translation_unit_using_function_calls;
	
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
	
	std::vector<std::string> cmake_module_paths;
	
	bool library_exists(const char * name) const
	{
		for (auto & library : libraries)
		{
			if (library->name == name)
				return true;
		}
		
		return false;
	}
	
	ChibiLibrary * find_library(const char * name) const
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
				if (match_wildcard(name, build_target.c_str(), ';'))
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
