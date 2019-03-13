#pragma once

#include <string>
#include <vector>

void show_chibi_syntax();

bool find_chibi_build_root(const char * source_path, char * build_root, const int build_root_size);

bool chibi_generate(const char * cwd, const char * src_path, const char * dst_path, const char ** targets, const int numTargets);

bool list_chibi_targets(const char * build_root, std::vector<std::string> & library_targets, std::vector<std::string> & app_targets);
