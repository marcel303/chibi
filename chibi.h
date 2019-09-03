#pragma once

#include <string>
#include <vector>

/**
 * Prints the chibi syntax to stdout.
 */
void show_chibi_syntax();

/**
 * Finds the chibi build root, starting the search at source_path, and storing the result in build_root.
 * If no build root is found, the function returns false.
 * @param source_path Location to start searching for the build root.
 * @param build_root Output location for the path of the found build root.
 * @param build_root_size Size of the build_root text buffer (including 0 character).
 * @return True if a build root is found. False otherwise.
 */
bool find_chibi_build_root(const char * source_path, char * build_root, const int build_root_size);

/**
 * Generates a CMakeLists.txt file, using the build root found starting at src_path. Upon success, the generated file
 * will be written to dst_path. Optionally, one or more target filters can be specified to include only a specific
 * subset of targets to generate apps and libraries for.
 * @param cwd The current working directory. TODO: WHY IS THIS NEEDED AGAIN?
 * @param src_path The path to start searching for the build root.
 * @param dst_path The target location for the generated CMakeLists.txt file.
 * @param targets One or more optional target filters, to limit the scope of the generated CMakeLists.txt file.
 * @param num_targets The number of elements of the targets array. CMake apps and libraries will be generated for all targets when zero.
 * @param platform The platform for which to generate the CMakeLists.txt file. By default this is determined by the OS for which chibi is compiled.
 * @return True if the CMakeLists.txt file was successfully generated. False otherwise.
 */
bool chibi_generate(const char * cwd, const char * src_path, const char * dst_path, const char ** targets, const int numTargets, const char * platform = nullptr);

/**
 * Lists all of the app and library targets found by parsing the given build root.
 * @param build_root The build root to parse.
 * @param library_targets Output array for the found library targets.
 * @param app_targets Output array for the found app targets.
 * @return True on success. False otherwise.
 */
bool list_chibi_targets(const char * build_root, std::vector<std::string> & library_targets, std::vector<std::string> & app_targets);
