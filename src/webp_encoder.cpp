#include "webp_encoder.h"

#include <stdio.h>

#include "output_file.h"
#include "webp/encode.h"

namespace
{

struct FileWriter
{
    FILE* file;
};

static int write_webp(const uint8_t* data, size_t data_size, const WebPPicture* picture)
{
    FileWriter* writer = (FileWriter*)picture->custom_ptr;
    if (!writer || !writer->file)
        return 0;

    return fwrite(data, 1, data_size, writer->file) == data_size;
}

static const char* encoding_error_message(WebPEncodingError error)
{
    switch (error)
    {
    case VP8_ENC_OK:
        return "ok";
    case VP8_ENC_ERROR_OUT_OF_MEMORY:
        return "out of memory";
    case VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY:
        return "bitstream out of memory";
    case VP8_ENC_ERROR_NULL_PARAMETER:
        return "null parameter";
    case VP8_ENC_ERROR_INVALID_CONFIGURATION:
        return "invalid configuration";
    case VP8_ENC_ERROR_BAD_DIMENSION:
        return "bad dimension";
    case VP8_ENC_ERROR_PARTITION0_OVERFLOW:
        return "partition 0 overflow";
    case VP8_ENC_ERROR_PARTITION_OVERFLOW:
        return "partition overflow";
    case VP8_ENC_ERROR_BAD_WRITE:
        return "output write failed";
    case VP8_ENC_ERROR_FILE_TOO_BIG:
        return "file too big";
    case VP8_ENC_ERROR_USER_ABORT:
        return "user abort";
    default:
        return "unknown encoding error";
    }
}

} // namespace

bool encode_webp_file(const path_t& output_path, int width, int height, int channels,
                      const unsigned char* pixels, const WebPOptions& options,
                      std::string& error)
{
    error.clear();

    if (!pixels)
    {
        error = "pixel buffer is null";
        return false;
    }
    if (width <= 0 || height <= 0 || width > WEBP_MAX_DIMENSION || height > WEBP_MAX_DIMENSION)
    {
        error = "image dimensions are outside libwebp limits";
        return false;
    }
    if (channels != 3 && channels != 4)
    {
        error = "unsupported channel count";
        return false;
    }

    WebPConfig config;
    if (!WebPConfigInit(&config))
    {
        error = "WebPConfigInit version mismatch";
        return false;
    }
    config.lossless = 0;
    config.quality = options.quality;
    config.method = options.method;
    config.thread_level = 0;
    if (!WebPValidateConfig(&config))
    {
        error = "invalid WebP configuration";
        return false;
    }

    WebPPicture picture;
    if (!WebPPictureInit(&picture))
    {
        error = "WebPPictureInit version mismatch";
        return false;
    }

    picture.width = width;
    picture.height = height;
    picture.use_argb = 1;

    int imported = 0;
#if _WIN32
    if (channels == 3)
        imported = WebPPictureImportBGR(&picture, pixels, width * channels);
    else
        imported = WebPPictureImportBGRA(&picture, pixels, width * channels);
#else
    if (channels == 3)
        imported = WebPPictureImportRGB(&picture, pixels, width * channels);
    else
        imported = WebPPictureImportRGBA(&picture, pixels, width * channels);
#endif
    if (!imported)
    {
        WebPPictureFree(&picture);
        error = "WebPPicture import failed";
        return false;
    }

    FILE* file = create_temporary_output(output_path, error);
    if (!file)
    {
        WebPPictureFree(&picture);
        return false;
    }

    FileWriter writer;
    writer.file = file;
    picture.writer = write_webp;
    picture.custom_ptr = &writer;

    bool success = WebPEncode(&config, &picture) != 0;
    if (!success)
        error = std::string("WebPEncode failed: ") + encoding_error_message(picture.error_code);

    if (success && (fflush(file) != 0 || ferror(file)))
    {
        success = false;
        error = "flushing temporary output failed";
    }

    if (fclose(file) != 0 && success)
    {
        success = false;
        error = "closing temporary output failed";
    }
    writer.file = 0;
    WebPPictureFree(&picture);

    if (!success)
    {
        discard_temporary_output(output_path);
        return false;
    }
    return commit_temporary_output(output_path, error);
}
