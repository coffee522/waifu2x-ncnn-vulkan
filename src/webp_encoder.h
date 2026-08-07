#ifndef WEBP_ENCODER_H
#define WEBP_ENCODER_H

#include <string>

#include "filesystem_utils.h"

path_t webp_temporary_path(const path_t& output_path);

bool encode_webp_file(const path_t& output_path, int width, int height, int channels,
                      const unsigned char* pixels, std::string& error);

#endif // WEBP_ENCODER_H
