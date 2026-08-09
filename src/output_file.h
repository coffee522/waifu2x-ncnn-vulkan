#ifndef OUTPUT_FILE_H
#define OUTPUT_FILE_H

#include <stdio.h>
#include <string>

#include "filesystem_utils.h"

path_t output_temporary_path(const path_t& output_path);
FILE* create_temporary_output(const path_t& output_path, std::string& error);
bool commit_temporary_output(const path_t& output_path, std::string& error);
void discard_temporary_output(const path_t& output_path);

#endif // OUTPUT_FILE_H
