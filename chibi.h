#pragma once

void show_chibi_syntax();

bool find_chibi_build_root(const char * source_path, char * build_root, const int build_root_size);

bool chibi_process(char * cwd, const char * src_path, const char * dst_path, const char ** targets, const int numTargets);
