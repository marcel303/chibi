#pragma once

void show_chibi_cli();
void show_chibi_syntax();

bool chibi_process(char * cwd, const char * src_path, const char * dst_path, const char ** targets, const int numTargets);
