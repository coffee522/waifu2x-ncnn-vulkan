// waifu2x implemented with ncnn library

#include <stdio.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cwctype>
#include <limits.h>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include <clocale>

#if _WIN32
// image decoder and encoder with wic
#include "wic_image.h"
#else // _WIN32
// image decoder and encoder with stb
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif // _WIN32
#include "webp_image.h"
#include "webp_encoder.h"
#include "output_file.h"

#if _WIN32
#include <wchar.h>
static wchar_t* optarg = NULL;
static int optind = 1;
static wchar_t getopt(int argc, wchar_t* const argv[], const wchar_t* optstring)
{
    if (optind >= argc || argv[optind][0] != L'-')
        return -1;

    wchar_t opt = argv[optind][1];
    const wchar_t* p = wcschr(optstring, opt);
    if (p == NULL)
        return L'?';

    optarg = NULL;

    if (p[1] == L':')
    {
        optind++;
        if (optind >= argc)
            return L'?';

        optarg = argv[optind];
    }

    optind++;

    return opt;
}

static std::vector<int> parse_optarg_int_array(const wchar_t* optarg)
{
    std::vector<int> array;
    array.push_back(_wtoi(optarg));

    const wchar_t* p = wcschr(optarg, L',');
    while (p)
    {
        p++;
        array.push_back(_wtoi(p));
        p = wcschr(p, L',');
    }

    return array;
}

static int parse_jobs_argument(const wchar_t* argument, int& jobs_load, std::vector<int>& jobs_proc, int& jobs_save)
{
    const wchar_t* first = wcschr(argument, L':');
    const wchar_t* second = first ? wcschr(first + 1, L':') : 0;
    if (!first || !second || first == argument || second == first + 1 || second[1] == L'\0' || wcschr(second + 1, L':'))
        return -1;

    jobs_load = _wtoi(argument);
    jobs_proc = parse_optarg_int_array(first + 1);
    jobs_save = _wtoi(second + 1);
    return 0;
}

static int parse_integer_argument(const wchar_t* argument, int& value)
{
    errno = 0;
    wchar_t* end = 0;
    const long parsed = wcstol(argument, &end, 10);
    if (errno != 0 || end == argument || *end != L'\0' || parsed < INT_MIN || parsed > INT_MAX)
        return -1;
    value = (int)parsed;
    return 0;
}
#else // _WIN32
#include <unistd.h> // getopt()

static std::vector<int> parse_optarg_int_array(const char* optarg)
{
    std::vector<int> array;
    array.push_back(atoi(optarg));

    const char* p = strchr(optarg, ',');
    while (p)
    {
        p++;
        array.push_back(atoi(p));
        p = strchr(p, ',');
    }

    return array;
}

static int parse_jobs_argument(const char* argument, int& jobs_load, std::vector<int>& jobs_proc, int& jobs_save)
{
    const char* first = strchr(argument, ':');
    const char* second = first ? strchr(first + 1, ':') : 0;
    if (!first || !second || first == argument || second == first + 1 || second[1] == '\0' || strchr(second + 1, ':'))
        return -1;

    jobs_load = atoi(argument);
    jobs_proc = parse_optarg_int_array(first + 1);
    jobs_save = atoi(second + 1);
    return 0;
}

static int parse_integer_argument(const char* argument, int& value)
{
    errno = 0;
    char* end = 0;
    const long parsed = strtol(argument, &end, 10);
    if (errno != 0 || end == argument || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
        return -1;
    value = (int)parsed;
    return 0;
}
#endif // _WIN32

// ncnn
#include "cpu.h"
#include "gpu.h"
#include "platform.h"

#include "waifu2x.h"

#include "filesystem_utils.h"

static void print_usage()
{
    fprintf(stdout, "Usage: waifu2x-ncnn-vulkan -i infile -o outfile [options]...\n");
    fprintf(stdout, "       waifu2x-ncnn-vulkan -l listfile [options]...\n\n");
    fprintf(stdout, "  -h                   show this help\n");
    fprintf(stdout, "  -v                   verbose output\n");
    fprintf(stdout, "  -i input-path        input image path (jpg/png/webp) or directory\n");
    fprintf(stdout, "  -o output-path       output image path or directory\n");
    fprintf(stdout, "  -l list-path         UTF-8 TSV with input-path<TAB>output-path per line\n");
    fprintf(stdout, "  -n noise-level       denoise level (-1/0/1/2/3, default=0)\n");
    fprintf(stdout, "  -s scale             upscale ratio (1/2/4/8/16/32, default=2)\n");
    fprintf(stdout, "  -t tile-size         tile size (>=32/0=auto, default=0) can be 0,0,0 for multi-gpu\n");
    fprintf(stdout, "  -m model-path        waifu2x model path (default=models-cunet)\n");
    fprintf(stdout, "  -g gpu-id            gpu device to use (-1=cpu, default=auto) can be 0,1,2 for multi-gpu\n");
    fprintf(stdout, "  -j load:proc:save    thread count for load/proc/save (default=1:2:2) can be 1:2,2,2:2 for multi-gpu\n");
    fprintf(stdout, "  -x                   enable tta mode\n");
    fprintf(stdout, "  -f format            output format (webp/png, default=webp)\n");
    fprintf(stdout, "  -q quality           WebP quality (0-100, default=85)\n");
    fprintf(stdout, "  -c method            WebP compression method (0-6, default=2)\n");
    fprintf(stdout, "\nWebP lossless=0 and thread_level=0 are fixed\n");
}

enum OutputFormat
{
    OUTPUT_WEBP,
    OUTPUT_PNG
};

static path_t lowercase_path(path_t value)
{
    for (size_t i = 0; i < value.size(); i++)
    {
#if _WIN32
        value[i] = (wchar_t)towlower(value[i]);
#else
        value[i] = (char)tolower((unsigned char)value[i]);
#endif
    }
    return value;
}

static int parse_output_format(const path_t& value, OutputFormat& format)
{
    const path_t normalized = lowercase_path(value);
    if (normalized == PATHSTR("webp"))
    {
        format = OUTPUT_WEBP;
        return 0;
    }
    if (normalized == PATHSTR("png"))
    {
        format = OUTPUT_PNG;
        return 0;
    }
    return -1;
}

static const char* output_format_name(OutputFormat format)
{
    return format == OUTPUT_WEBP ? "WebP" : "PNG";
}

static path_t output_extension(OutputFormat format)
{
    return format == OUTPUT_WEBP ? PATHSTR("webp") : PATHSTR("png");
}

static int is_supported_input_path(const path_t& path)
{
    path_t ext = get_file_extension(path);
    return ext == PATHSTR("jpg") || ext == PATHSTR("JPG")
        || ext == PATHSTR("jpeg") || ext == PATHSTR("JPEG")
        || ext == PATHSTR("png") || ext == PATHSTR("PNG")
        || ext == PATHSTR("webp") || ext == PATHSTR("WEBP");
}

static int is_output_path(const path_t& path, OutputFormat format)
{
    return lowercase_path(get_file_extension(path)) == output_extension(format);
}

static int utf8_to_path(const std::string& text, path_t& path)
{
#if _WIN32
    if (text.empty())
    {
        path.clear();
        return 0;
    }

    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), (int)text.size(), 0, 0);
    if (length <= 0)
        return -1;

    std::vector<wchar_t> buffer(length);
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), (int)text.size(), &buffer[0], length);
    if (length <= 0)
        return -1;

    path.assign(buffer.begin(), buffer.end());
#else
    path = text;
#endif

    return 0;
}

static int load_path_list(const path_t& listpath, OutputFormat format,
                          std::vector<path_t>& input_files, std::vector<path_t>& output_files)
{
#if _WIN32
    FILE* fp = _wfopen(listpath.c_str(), L"rb");
#else
    FILE* fp = fopen(listpath.c_str(), "rb");
#endif
    if (!fp)
    {
#if _WIN32
        fwprintf(stderr, L"open list file %ls failed\n", listpath.c_str());
#else
        fprintf(stderr, "open list file %s failed\n", listpath.c_str());
#endif
        return -1;
    }

    std::string content;
    char buffer[4096];
    for (;;)
    {
        size_t bytes = fread(buffer, 1, sizeof(buffer), fp);
        if (bytes > 0)
            content.append(buffer, bytes);

        if (bytes < sizeof(buffer))
        {
            if (ferror(fp))
            {
                fclose(fp);
                fprintf(stderr, "read list file failed\n");
                return -1;
            }
            break;
        }
    }
    fclose(fp);

    size_t pos = 0;
    int line_number = 1;
    while (pos <= content.size())
    {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos)
            end = content.size();

        std::string line = content.substr(pos, end - pos);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.resize(line.size() - 1);

        if (line_number == 1 && line.size() >= 3
                && (unsigned char)line[0] == 0xef
                && (unsigned char)line[1] == 0xbb
                && (unsigned char)line[2] == 0xbf)
        {
            line.erase(0, 3);
        }

        if (!line.empty() && line[0] != '#')
        {
            size_t tab = line.find('\t');
            if (tab == std::string::npos || tab == 0 || tab == line.size() - 1 || line.find('\t', tab + 1) != std::string::npos)
            {
                fprintf(stderr, "invalid list file line %d\n", line_number);
                return -1;
            }

            path_t input_path;
            path_t output_path;
            if (utf8_to_path(line.substr(0, tab), input_path) != 0 || utf8_to_path(line.substr(tab + 1), output_path) != 0)
            {
                fprintf(stderr, "invalid UTF-8 in list file line %d\n", line_number);
                return -1;
            }
            if (!is_supported_input_path(input_path))
            {
                fprintf(stderr, "unsupported input extension in list file line %d\n", line_number);
                return -1;
            }
            if (!is_output_path(output_path, format))
            {
                fprintf(stderr, "output path extension does not match -f in list file line %d\n", line_number);
                return -1;
            }

            input_files.push_back(input_path);
            output_files.push_back(output_path);
        }

        if (end == content.size())
            break;

        pos = end + 1;
        line_number++;
    }

    if (input_files.empty())
    {
        fprintf(stderr, "list file has no input/output pairs\n");
        return -1;
    }

    return 0;
}

static int validate_paths(const std::vector<path_t>& input_files,
                          const std::vector<path_t>& output_files, OutputFormat format)
{
    if (input_files.empty() || input_files.size() != output_files.size())
    {
        fprintf(stderr, "input/output file list is empty or mismatched\n");
        return -1;
    }

    std::set<path_t> unique_outputs;
    for (size_t i = 0; i < input_files.size(); i++)
    {
        const path_t& input_path = input_files[i];
        const path_t& output_path = output_files[i];
        const path_t temporary_path = output_temporary_path(output_path);

        if (!is_supported_input_path(input_path) || path_is_directory(input_path) || !filepath_is_readable(input_path))
        {
#if _WIN32
            fwprintf(stderr, L"input image is missing or unreadable: %ls\n", input_path.c_str());
#else
            fprintf(stderr, "input image is missing or unreadable: %s\n", input_path.c_str());
#endif
            return -1;
        }
        if (!is_output_path(output_path, format))
        {
            fprintf(stderr, "all output file extensions must match -f\n");
            return -1;
        }
        if (!path_is_directory(get_parent_directory(output_path)))
        {
#if _WIN32
            fwprintf(stderr, L"output parent directory does not exist: %ls\n", get_parent_directory(output_path).c_str());
#else
            fprintf(stderr, "output parent directory does not exist: %s\n", get_parent_directory(output_path).c_str());
#endif
            return -1;
        }
        if (path_exists(output_path) || path_exists(temporary_path))
        {
#if _WIN32
            fwprintf(stderr, L"output or temporary output already exists: %ls\n", output_path.c_str());
#else
            fprintf(stderr, "output or temporary output already exists: %s\n", output_path.c_str());
#endif
            return -1;
        }
        if (!unique_outputs.insert(output_path).second)
        {
#if _WIN32
            fwprintf(stderr, L"duplicate output path: %ls\n", output_path.c_str());
#else
            fprintf(stderr, "duplicate output path: %s\n", output_path.c_str());
#endif
            return -1;
        }
    }

    return 0;
}

class JobStatus
{
public:
    explicit JobStatus(int expected_count)
        : expected(expected_count), loaded(0), processed(0), encoded(0), failed(0)
    {
    }

    const int expected;
    std::atomic<int> loaded;
    std::atomic<int> processed;
    std::atomic<int> encoded;
    std::atomic<int> failed;
};

class Task
{
public:
    int id;
    int input_is_webp;
    int scale;

    path_t inpath;
    path_t outpath;

    ncnn::Mat inimage;
    ncnn::Mat outimage;
};

class TaskQueue
{
public:
    TaskQueue()
    {
    }

    void put(const Task& v)
    {
        lock.lock();

        while (tasks.size() >= 8) // FIXME hardcode queue length
        {
            condition.wait(lock);
        }

        tasks.push(v);

        lock.unlock();

        condition.signal();
    }

    void get(Task& v)
    {
        lock.lock();

        while (tasks.size() == 0)
        {
            condition.wait(lock);
        }

        v = tasks.front();
        tasks.pop();

        lock.unlock();

        condition.signal();
    }

private:
    ncnn::Mutex lock;
    ncnn::ConditionVariable condition;
    std::queue<Task> tasks;
};

TaskQueue toproc;
TaskQueue tosave;

static void release_input_pixels(Task& task)
{
    unsigned char* pixeldata = (unsigned char*)task.inimage.data;
    if (!pixeldata)
        return;

    if (task.input_is_webp == 1)
    {
        free(pixeldata);
    }
    else
    {
#if _WIN32
        free(pixeldata);
#else
        stbi_image_free(pixeldata);
#endif
    }

    task.inimage = ncnn::Mat();
}

static unsigned char* read_file_bytes(const path_t& path, int& length)
{
    length = 0;

#if _WIN32
    FILE* fp = _wfopen(path.c_str(), L"rb");
#else
    FILE* fp = fopen(path.c_str(), "rb");
#endif
    if (!fp)
        return 0;

    unsigned char* data = 0;
    if (fseek(fp, 0, SEEK_END) == 0)
    {
        long file_length = ftell(fp);
        if (file_length > 0 && file_length <= INT_MAX && fseek(fp, 0, SEEK_SET) == 0)
        {
            length = (int)file_length;
            data = (unsigned char*)malloc(length);
            if (data && fread(data, 1, length, fp) != (size_t)length)
            {
                free(data);
                data = 0;
                length = 0;
            }
        }
    }

    fclose(fp);
    return data;
}

class LoadThreadParams
{
public:
    int scale;
    int jobs_load;
    JobStatus* status;

    // session data
    std::vector<path_t> input_files;
    std::vector<path_t> output_files;
};

void* load(void* args)
{
    const LoadThreadParams* ltp = (const LoadThreadParams*)args;
    const int count = (int)ltp->input_files.size();
    const int scale = ltp->scale;
    JobStatus* status = ltp->status;

    #pragma omp parallel for schedule(static,1) num_threads(ltp->jobs_load)
    for (int i=0; i<count; i++)
    {
        const path_t& imagepath = ltp->input_files[i];

        int input_is_webp = 0;

        unsigned char* pixeldata = 0;
        int w = 0;
        int h = 0;
        int c = 0;

#if _WIN32
        path_t extension = get_file_extension(imagepath);
        if (extension == PATHSTR("webp") || extension == PATHSTR("WEBP"))
        {
            int length = 0;
            unsigned char* filedata = read_file_bytes(imagepath, length);
            if (filedata)
            {
                pixeldata = webp_load(filedata, length, &w, &h, &c);
                free(filedata);
                if (pixeldata)
                    input_is_webp = 1;
            }
        }
        else
        {
            pixeldata = wic_decode_image(imagepath.c_str(), &w, &h, &c);
        }
#else
        int length = 0;
        unsigned char* filedata = read_file_bytes(imagepath, length);
        if (filedata)
        {
            pixeldata = webp_load(filedata, length, &w, &h, &c);
            if (pixeldata)
            {
                input_is_webp = 1;
            }
            else
            {
                pixeldata = stbi_load_from_memory(filedata, length, &w, &h, &c, 0);
                if (pixeldata)
                {
                    // stb_image auto channel
                    if (c == 1)
                    {
                        // grayscale -> rgb
                        stbi_image_free(pixeldata);
                        pixeldata = stbi_load_from_memory(filedata, length, &w, &h, &c, 3);
                        c = 3;
                    }
                    else if (c == 2)
                    {
                        // grayscale + alpha -> rgba
                        stbi_image_free(pixeldata);
                        pixeldata = stbi_load_from_memory(filedata, length, &w, &h, &c, 4);
                        c = 4;
                    }
                }
            }

            free(filedata);
        }
#endif
        if (pixeldata)
        {
            Task v;
            v.id = i;
            v.input_is_webp = input_is_webp;
            v.scale = scale;
            v.inpath = imagepath;
            v.outpath = ltp->output_files[i];

            v.inimage = ncnn::Mat(w, h, (void*)pixeldata, (size_t)c, c);

            status->loaded.fetch_add(1);
            toproc.put(v);
        }
        else
        {
#if _WIN32
            fwprintf(stderr, L"decode image %ls failed\n", imagepath.c_str());
#else // _WIN32
            fprintf(stderr, "decode image %s failed\n", imagepath.c_str());
#endif // _WIN32
            status->failed.fetch_add(1);
        }
    }

    return 0;
}

class ProcThreadParams
{
public:
    const Waifu2x* waifu2x;
    JobStatus* status;
};

void* proc(void* args)
{
    const ProcThreadParams* ptp = (const ProcThreadParams*)args;
    const Waifu2x* waifu2x = ptp->waifu2x;
    JobStatus* status = ptp->status;

    for (;;)
    {
        Task v;

        toproc.get(v);

        if (v.id == -233)
            break;

        const int scale = v.scale;
        bool success = true;
        if (scale == 1)
        {
            v.outimage = ncnn::Mat(v.inimage.w, v.inimage.h, (size_t)v.inimage.elemsize, (int)v.inimage.elemsize);
            success = !v.outimage.empty() && waifu2x->process(v.inimage, v.outimage) == 0;
        }
        else
        {
            int scale_run_count = 0;
            if (scale == 2) scale_run_count = 1;
            if (scale == 4) scale_run_count = 2;
            if (scale == 8) scale_run_count = 3;
            if (scale == 16) scale_run_count = 4;
            if (scale == 32) scale_run_count = 5;

            v.outimage = ncnn::Mat(v.inimage.w * 2, v.inimage.h * 2, (size_t)v.inimage.elemsize, (int)v.inimage.elemsize);
            success = !v.outimage.empty() && waifu2x->process(v.inimage, v.outimage) == 0;

            for (int i = 1; success && i < scale_run_count; i++)
            {
                ncnn::Mat tmp = v.outimage;
                v.outimage = ncnn::Mat(tmp.w * 2, tmp.h * 2, (size_t)v.inimage.elemsize, (int)v.inimage.elemsize);
                success = !v.outimage.empty() && waifu2x->process(tmp, v.outimage) == 0;
            }
        }

        if (!success)
        {
#if _WIN32
            fwprintf(stderr, L"process image %ls failed\n", v.inpath.c_str());
#else
            fprintf(stderr, "process image %s failed\n", v.inpath.c_str());
#endif
            status->failed.fetch_add(1);
            release_input_pixels(v);
            continue;
        }

        status->processed.fetch_add(1);
        tosave.put(v);
    }

    return 0;
}

class SaveThreadParams
{
public:
    int verbose;
    JobStatus* status;
    OutputFormat output_format;
    WebPOptions webp;
};

static bool encode_png_file(const path_t& output_path, int width, int height, int channels,
                            const unsigned char* pixels, std::string& error)
{
    error.clear();
    if (!pixels)
    {
        error = "pixel buffer is null";
        return false;
    }
    if (width <= 0 || height <= 0 || (channels != 3 && channels != 4))
    {
        error = "invalid PNG dimensions or channel count";
        return false;
    }

    FILE* reservation = create_temporary_output(output_path, error);
    if (!reservation)
        return false;
    if (fclose(reservation) != 0)
    {
        discard_temporary_output(output_path);
        error = "closing reserved temporary output failed";
        return false;
    }

    const path_t temporary_path = output_temporary_path(output_path);
#if _WIN32
    const int encoded = wic_encode_image(temporary_path.c_str(), width, height, channels, (void*)pixels);
#else
    const int encoded = stbi_write_png(temporary_path.c_str(), width, height, channels,
                                       pixels, width * channels);
#endif
    if (!encoded)
    {
        discard_temporary_output(output_path);
        error = "PNG encoder failed";
        return false;
    }
    return commit_temporary_output(output_path, error);
}

void* save(void* args)
{
    const SaveThreadParams* stp = (const SaveThreadParams*)args;
    const int verbose = stp->verbose;
    JobStatus* status = stp->status;

    for (;;)
    {
        Task v;

        tosave.get(v);

        if (v.id == -233)
            break;

        std::string error;
        bool success = false;
        if (stp->output_format == OUTPUT_WEBP)
        {
            success = encode_webp_file(v.outpath, v.outimage.w, v.outimage.h,
                                       v.outimage.elempack, (const unsigned char*)v.outimage.data,
                                       stp->webp, error);
        }
        else
        {
            success = encode_png_file(v.outpath, v.outimage.w, v.outimage.h,
                                      v.outimage.elempack, (const unsigned char*)v.outimage.data,
                                      error);
        }
        release_input_pixels(v);

        if (success)
        {
            status->encoded.fetch_add(1);
            if (verbose)
            {
#if _WIN32
                fwprintf(stdout, L"%ls -> %ls done\n", v.inpath.c_str(), v.outpath.c_str());
#else
                fprintf(stdout, "%s -> %s done\n", v.inpath.c_str(), v.outpath.c_str());
#endif
            }
        }
        else
        {
#if _WIN32
            fwprintf(stderr, L"encode image %ls failed: %hs\n", v.outpath.c_str(), error.c_str());
#else
            fprintf(stderr, "encode image %s failed: %s\n", v.outpath.c_str(), error.c_str());
#endif
            status->failed.fetch_add(1);
        }
    }

    return 0;
}


#if _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    path_t inputpath;
    path_t outputpath;
    path_t listpath;
    int noise = 0;
    int scale = 2;
    std::vector<int> tilesize;
    path_t model = PATHSTR("models-cunet");
    std::vector<int> gpuid;
    int jobs_load = 1;
    std::vector<int> jobs_proc;
    int jobs_save = 2;
    int verbose = 0;
    int tta_mode = 0;
    OutputFormat output_format = OUTPUT_WEBP;
    WebPOptions webp_options = {85.f, 2};
    bool webp_quality_specified = false;
    bool webp_method_specified = false;

#if _WIN32
    setlocale(LC_ALL, "");
    wchar_t opt;
    while ((opt = getopt(argc, argv, L"i:o:l:n:s:t:m:g:j:f:q:c:vxh")) != (wchar_t)-1)
    {
        switch (opt)
        {
        case L'i':
            inputpath = optarg;
            break;
        case L'o':
            outputpath = optarg;
            break;
        case L'l':
            listpath = optarg;
            break;
        case L'n':
            noise = _wtoi(optarg);
            break;
        case L's':
            scale = _wtoi(optarg);
            break;
        case L't':
            tilesize = parse_optarg_int_array(optarg);
            break;
        case L'm':
            model = optarg;
            break;
        case L'g':
            gpuid = parse_optarg_int_array(optarg);
            break;
        case L'j':
            if (parse_jobs_argument(optarg, jobs_load, jobs_proc, jobs_save) != 0)
            {
                fprintf(stderr, "invalid thread count argument\n");
                return -1;
            }
            break;
        case L'f':
            if (parse_output_format(path_t(optarg), output_format) != 0)
            {
                fprintf(stderr, "invalid output format; expected webp or png\n");
                return -1;
            }
            break;
        case L'q':
        {
            int quality = 0;
            if (parse_integer_argument(optarg, quality) != 0)
            {
                fprintf(stderr, "invalid WebP quality argument\n");
                return -1;
            }
            webp_options.quality = (float)quality;
            webp_quality_specified = true;
            break;
        }
        case L'c':
            if (parse_integer_argument(optarg, webp_options.method) != 0)
            {
                fprintf(stderr, "invalid WebP method argument\n");
                return -1;
            }
            webp_method_specified = true;
            break;
        case L'v':
            verbose = 1;
            break;
        case L'x':
            tta_mode = 1;
            break;
        case L'h':
            print_usage();
            return 0;
        default:
            print_usage();
            return -1;
        }
    }
#else // _WIN32
    int opt;
    while ((opt = getopt(argc, argv, "i:o:l:n:s:t:m:g:j:f:q:c:vxh")) != -1)
    {
        switch (opt)
        {
        case 'i':
            inputpath = optarg;
            break;
        case 'o':
            outputpath = optarg;
            break;
        case 'l':
            listpath = optarg;
            break;
        case 'n':
            noise = atoi(optarg);
            break;
        case 's':
            scale = atoi(optarg);
            break;
        case 't':
            tilesize = parse_optarg_int_array(optarg);
            break;
        case 'm':
            model = optarg;
            break;
        case 'g':
            gpuid = parse_optarg_int_array(optarg);
            break;
        case 'j':
            if (parse_jobs_argument(optarg, jobs_load, jobs_proc, jobs_save) != 0)
            {
                fprintf(stderr, "invalid thread count argument\n");
                return -1;
            }
            break;
        case 'f':
            if (parse_output_format(path_t(optarg), output_format) != 0)
            {
                fprintf(stderr, "invalid output format; expected webp or png\n");
                return -1;
            }
            break;
        case 'q':
        {
            int quality = 0;
            if (parse_integer_argument(optarg, quality) != 0)
            {
                fprintf(stderr, "invalid WebP quality argument\n");
                return -1;
            }
            webp_options.quality = (float)quality;
            webp_quality_specified = true;
            break;
        }
        case 'c':
            if (parse_integer_argument(optarg, webp_options.method) != 0)
            {
                fprintf(stderr, "invalid WebP method argument\n");
                return -1;
            }
            webp_method_specified = true;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'x':
            tta_mode = 1;
            break;
        case 'h':
            print_usage();
            return 0;
        default:
            print_usage();
            return -1;
        }
    }
#endif // _WIN32

    if (!listpath.empty() && (!inputpath.empty() || !outputpath.empty()))
    {
        fprintf(stderr, "list file mode cannot be combined with -i or -o\n");
        return -1;
    }

    if (listpath.empty() && (inputpath.empty() || outputpath.empty()))
    {
        print_usage();
        return -1;
    }

    if (noise < -1 || noise > 3)
    {
        fprintf(stderr, "invalid noise argument\n");
        return -1;
    }

    if (!(scale == 1 || scale == 2 || scale == 4 || scale == 8 || scale == 16 || scale == 32))
    {
        fprintf(stderr, "invalid scale argument\n");
        return -1;
    }

    if (tilesize.size() != (gpuid.empty() ? 1 : gpuid.size()) && !tilesize.empty())
    {
        fprintf(stderr, "invalid tilesize argument\n");
        return -1;
    }

    for (int i=0; i<(int)tilesize.size(); i++)
    {
        if (tilesize[i] != 0 && tilesize[i] < 32)
        {
            fprintf(stderr, "invalid tilesize argument\n");
            return -1;
        }
    }

    if (jobs_load < 1 || jobs_save < 1)
    {
        fprintf(stderr, "invalid thread count argument\n");
        return -1;
    }

    if (jobs_proc.size() != (gpuid.empty() ? 1 : gpuid.size()) && !jobs_proc.empty())
    {
        fprintf(stderr, "invalid jobs_proc thread count argument\n");
        return -1;
    }

    for (int i=0; i<(int)jobs_proc.size(); i++)
    {
        if (jobs_proc[i] < 1)
        {
            fprintf(stderr, "invalid jobs_proc thread count argument\n");
            return -1;
        }
    }

    if (webp_options.quality < 0.f || webp_options.quality > 100.f)
    {
        fprintf(stderr, "WebP quality must be between 0 and 100\n");
        return -1;
    }
    if (webp_options.method < 0 || webp_options.method > 6)
    {
        fprintf(stderr, "WebP method must be between 0 and 6\n");
        return -1;
    }
    if (output_format == OUTPUT_PNG && (webp_quality_specified || webp_method_specified))
    {
        fprintf(stderr, "-q and -c are only valid with -f webp\n");
        return -1;
    }

    if (listpath.empty() && !path_is_directory(outputpath) && !is_output_path(outputpath, output_format))
    {
        fprintf(stderr, "output file extension must match -f\n");
        return -1;
    }

    // collect input and output filepath
    std::vector<path_t> input_files;
    std::vector<path_t> output_files;
    {
        if (!listpath.empty())
        {
            if (load_path_list(listpath, output_format, input_files, output_files) != 0)
                return -1;
        }
        else if (path_is_directory(inputpath) && path_is_directory(outputpath))
        {
            std::vector<path_t> filenames;
            int lr = list_directory(inputpath, filenames);
            if (lr != 0)
                return -1;

            std::vector<path_t> image_filenames;
            for (size_t i = 0; i < filenames.size(); i++)
            {
                if (is_supported_input_path(filenames[i]))
                    image_filenames.push_back(filenames[i]);
            }

            const int count = (int)image_filenames.size();
            input_files.resize(count);
            output_files.resize(count);

            path_t last_filename;
            path_t last_filename_noext;
            for (int i=0; i<count; i++)
            {
                path_t filename = image_filenames[i];
                path_t filename_noext = get_file_name_without_extension(filename);
                path_t output_filename = filename_noext + PATHSTR(".") + output_extension(output_format);

                // filename list is sorted, check if output image path conflicts
                if (filename_noext == last_filename_noext)
                {
                    path_t output_filename2 = filename + PATHSTR(".") + output_extension(output_format);
#if _WIN32
                    fwprintf(stderr, L"both %ls and %ls output %ls ! %ls will output %ls\n", filename.c_str(), last_filename.c_str(), output_filename.c_str(), filename.c_str(), output_filename2.c_str());
#else
                    fprintf(stderr, "both %s and %s output %s ! %s will output %s\n", filename.c_str(), last_filename.c_str(), output_filename.c_str(), filename.c_str(), output_filename2.c_str());
#endif
                    output_filename = output_filename2;
                }
                else
                {
                    last_filename = filename;
                    last_filename_noext = filename_noext;
                }

                input_files[i] = inputpath + PATHSTR('/') + filename;
                output_files[i] = outputpath + PATHSTR('/') + output_filename;
            }
        }
        else if (!path_is_directory(inputpath) && !path_is_directory(outputpath))
        {
            input_files.push_back(inputpath);
            output_files.push_back(outputpath);
        }
        else
        {
            fprintf(stderr, "inputpath and outputpath must be either file or directory at the same time\n");
            return -1;
        }
    }

    if (validate_paths(input_files, output_files, output_format) != 0)
        return -1;

    int prepadding = 0;

    if (model.find(PATHSTR("models-cunet")) != path_t::npos)
    {
        if (noise == -1)
        {
            prepadding = 18;
        }
        else if (scale == 1)
        {
            prepadding = 28;
        }
        else if (scale == 2 || scale == 4 || scale == 8 || scale == 16 || scale == 32)
        {
            prepadding = 18;
        }
    }
    else if (model.find(PATHSTR("models-upconv_7_anime_style_art_rgb")) != path_t::npos)
    {
        prepadding = 7;
    }
    else if (model.find(PATHSTR("models-upconv_7_photo")) != path_t::npos)
    {
        prepadding = 7;
    }
    else
    {
        fprintf(stderr, "unknown model dir type\n");
        return -1;
    }

#if _WIN32
    wchar_t parampath[256];
    wchar_t modelpath[256];
    if (noise == -1)
    {
        swprintf(parampath, 256, L"%s/scale2.0x_model.param", model.c_str());
        swprintf(modelpath, 256, L"%s/scale2.0x_model.bin", model.c_str());
    }
    else if (scale == 1)
    {
        swprintf(parampath, 256, L"%s/noise%d_model.param", model.c_str(), noise);
        swprintf(modelpath, 256, L"%s/noise%d_model.bin", model.c_str(), noise);
    }
    else if (scale == 2 || scale == 4 || scale == 8 || scale == 16 || scale == 32)
    {
        swprintf(parampath, 256, L"%s/noise%d_scale2.0x_model.param", model.c_str(), noise);
        swprintf(modelpath, 256, L"%s/noise%d_scale2.0x_model.bin", model.c_str(), noise);
    }
#else
    char parampath[256];
    char modelpath[256];
    if (noise == -1)
    {
        sprintf(parampath, "%s/scale2.0x_model.param", model.c_str());
        sprintf(modelpath, "%s/scale2.0x_model.bin", model.c_str());
    }
    else if (scale == 1)
    {
        sprintf(parampath, "%s/noise%d_model.param", model.c_str(), noise);
        sprintf(modelpath, "%s/noise%d_model.bin", model.c_str(), noise);
    }
    else if (scale == 2 || scale == 4 || scale == 8 || scale == 16 || scale == 32)
    {
        sprintf(parampath, "%s/noise%d_scale2.0x_model.param", model.c_str(), noise);
        sprintf(modelpath, "%s/noise%d_scale2.0x_model.bin", model.c_str(), noise);
    }
#endif

    path_t paramfullpath = sanitize_filepath(parampath);
    path_t modelfullpath = sanitize_filepath(modelpath);

#if _WIN32
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

    bool gpu_instance_created = gpuid.empty();
    for (size_t i = 0; i < gpuid.size(); i++)
    {
        if (gpuid[i] >= 0)
            gpu_instance_created = true;
    }
    if (gpu_instance_created)
        ncnn::create_gpu_instance();

    if (gpuid.empty())
    {
        gpuid.push_back(ncnn::get_default_gpu_index());
    }

    const int use_gpu_count = (int)gpuid.size();

    if (jobs_proc.empty())
    {
        jobs_proc.resize(use_gpu_count, 2);
    }

    if (tilesize.empty())
    {
        tilesize.resize(use_gpu_count, 0);
    }

    int cpu_count = std::max(1, ncnn::get_cpu_count());
    jobs_load = std::min(jobs_load, cpu_count);
    jobs_save = std::min(jobs_save, cpu_count);

    int gpu_count = gpu_instance_created ? ncnn::get_gpu_count() : 0;
    for (int i=0; i<use_gpu_count; i++)
    {
        if (gpuid[i] < -1 || gpuid[i] >= gpu_count)
        {
            fprintf(stderr, "invalid gpu device\n");

            if (gpu_instance_created)
                ncnn::destroy_gpu_instance();
            return -1;
        }
    }

    int total_jobs_proc = 0;
    int jobs_proc_per_gpu[16] = {0};
    for (int i=0; i<use_gpu_count; i++)
    {
        if (gpuid[i] == -1)
        {
            jobs_proc[i] = std::min(jobs_proc[i], cpu_count);
            total_jobs_proc += 1;
        }
        else
        {
            total_jobs_proc += jobs_proc[i];
            jobs_proc_per_gpu[gpuid[i]] += jobs_proc[i];
        }
    }

    for (int i=0; i<use_gpu_count; i++)
    {
        if (tilesize[i] != 0)
            continue;

        if (gpuid[i] == -1)
        {
            // cpu only
            tilesize[i] = 400;
            continue;
        }

        uint32_t heap_budget = ncnn::get_gpu_device(gpuid[i])->get_heap_budget();

        if (input_files.size() > 1)
        {
            // multiple gpu jobs share the same heap
            heap_budget /= jobs_proc_per_gpu[gpuid[i]];
        }

        // more fine-grained tilesize policy here
        if (model.find(PATHSTR("models-cunet")) != path_t::npos)
        {
            if (heap_budget > 2600)
                tilesize[i] = 400;
            else if (heap_budget > 740)
                tilesize[i] = 200;
            else if (heap_budget > 250)
                tilesize[i] = 100;
            else
                tilesize[i] = 32;
        }
        else if (model.find(PATHSTR("models-upconv_7_anime_style_art_rgb")) != path_t::npos
            || model.find(PATHSTR("models-upconv_7_photo")) != path_t::npos)
        {
            if (heap_budget > 1900)
                tilesize[i] = 400;
            else if (heap_budget > 550)
                tilesize[i] = 200;
            else if (heap_budget > 190)
                tilesize[i] = 100;
            else
                tilesize[i] = 32;
        }
    }

    JobStatus job_status((int)input_files.size());
    int model_load_failed = 0;
    {
        std::vector<Waifu2x*> waifu2x(use_gpu_count);

        for (int i=0; i<use_gpu_count; i++)
        {
            int num_threads = gpuid[i] == -1 ? jobs_proc[i] : 1;

            waifu2x[i] = new Waifu2x(gpuid[i], tta_mode, num_threads);

            if (waifu2x[i]->load(paramfullpath, modelfullpath) != 0)
            {
#if _WIN32
                fwprintf(stderr, L"load waifu2x model failed\n");
#else
                fprintf(stderr, "load waifu2x model failed\n");
#endif
                model_load_failed = 1;
                break;
            }

            waifu2x[i]->noise = noise;
            waifu2x[i]->scale = (scale >= 2) ? 2 : scale;
            waifu2x[i]->tilesize = tilesize[i];
            waifu2x[i]->prepadding = prepadding;
        }

        // main routine
        if (!model_load_failed)
        {
            // load image
            LoadThreadParams ltp;
            ltp.scale = scale;
            ltp.jobs_load = jobs_load;
            ltp.status = &job_status;
            ltp.input_files = input_files;
            ltp.output_files = output_files;

            ncnn::Thread load_thread(load, (void*)&ltp);

            // waifu2x proc
            std::vector<ProcThreadParams> ptp(use_gpu_count);
            for (int i=0; i<use_gpu_count; i++)
            {
                ptp[i].waifu2x = waifu2x[i];
                ptp[i].status = &job_status;
            }

            std::vector<ncnn::Thread*> proc_threads(total_jobs_proc);
            {
                int total_jobs_proc_id = 0;
                for (int i=0; i<use_gpu_count; i++)
                {
                    if (gpuid[i] == -1)
                    {
                        proc_threads[total_jobs_proc_id++] = new ncnn::Thread(proc, (void*)&ptp[i]);
                    }
                    else
                    {
                        for (int j=0; j<jobs_proc[i]; j++)
                        {
                            proc_threads[total_jobs_proc_id++] = new ncnn::Thread(proc, (void*)&ptp[i]);
                        }
                    }
                }
            }

            // save image
            SaveThreadParams stp;
            stp.verbose = verbose;
            stp.status = &job_status;
            stp.output_format = output_format;
            stp.webp = webp_options;

            std::vector<ncnn::Thread*> save_threads(jobs_save);
            for (int i=0; i<jobs_save; i++)
            {
                save_threads[i] = new ncnn::Thread(save, (void*)&stp);
            }

            // end
            load_thread.join();

            Task end;
            end.id = -233;

            for (int i=0; i<total_jobs_proc; i++)
            {
                toproc.put(end);
            }

            for (int i=0; i<total_jobs_proc; i++)
            {
                proc_threads[i]->join();
                delete proc_threads[i];
            }

            for (int i=0; i<jobs_save; i++)
            {
                tosave.put(end);
            }

            for (int i=0; i<jobs_save; i++)
            {
                save_threads[i]->join();
                delete save_threads[i];
            }
        }

        for (int i=0; i<use_gpu_count; i++)
        {
            if (waifu2x[i])
                delete waifu2x[i];
        }
        waifu2x.clear();
    }

    if (gpu_instance_created)
        ncnn::destroy_gpu_instance();

    if (model_load_failed)
        return 1;

    const int loaded = job_status.loaded.load();
    const int processed = job_status.processed.load();
    const int encoded = job_status.encoded.load();
    const int failed = job_status.failed.load();
    if (failed != 0 || loaded != job_status.expected || processed != job_status.expected || encoded != job_status.expected)
    {
#if _WIN32
        fwprintf(stderr, L"job failed expected=%d loaded=%d processed=%d encoded=%d failed=%d\n",
                 job_status.expected, loaded, processed, encoded, failed);
#else
        fprintf(stderr, "job failed expected=%d loaded=%d processed=%d encoded=%d failed=%d\n",
                job_status.expected, loaded, processed, encoded, failed);
#endif
        return 1;
    }

    if (output_format == OUTPUT_WEBP)
    {
        fprintf(stdout, "completed %d images as %s (quality=%.0f method=%d lossless=0 thread_level=0)\n",
                encoded, output_format_name(output_format), webp_options.quality, webp_options.method);
    }
    else
    {
        fprintf(stdout, "completed %d images as %s\n", encoded, output_format_name(output_format));
    }
    return 0;
}
