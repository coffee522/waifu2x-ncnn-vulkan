#ifndef WEBP_ENCODER_H
#define WEBP_ENCODER_H

#include <string>

#include "filesystem_utils.h"

struct WebPOptions
{
    float quality;
    int method;
};

bool encode_webp_file(const path_t& output_path, int width, int height, int channels,
                      const unsigned char* pixels, const WebPOptions& options,
                      std::string& error);

#endif // WEBP_ENCODER_H
