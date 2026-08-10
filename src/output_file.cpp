#include "output_file.h"

#include <errno.h>
#include <string.h>

#if _WIN32
#include <windows.h>
#endif

namespace
{

static std::string errno_message()
{
#if _WIN32
    char buffer[256];
    if (strerror_s(buffer, sizeof(buffer), errno) == 0)
        return buffer;
    return "unknown system error";
#else
    return strerror(errno);
#endif
}

static int remove_path(const path_t& path)
{
#if _WIN32
    return _wremove(path.c_str());
#else
    return remove(path.c_str());
#endif
}

static bool replace_path(const path_t& source, const path_t& destination, std::string& error)
{
#if _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    error = "committing temporary output failed: Windows error "
        + std::to_string((unsigned long)GetLastError());
#else
    if (rename(source.c_str(), destination.c_str()) == 0)
        return true;
    error = std::string("committing temporary output failed: ") + errno_message();
#endif
    return false;
}

} // namespace

path_t output_temporary_path(const path_t& output_path)
{
    return output_path + PATHSTR(".tmp");
}

FILE* create_temporary_output(const path_t& output_path, std::string& error)
{
    error.clear();
    const path_t temporary_path = output_temporary_path(output_path);
#if _WIN32
    FILE* file = 0;
    if (_wfopen_s(&file, temporary_path.c_str(), L"wbx") != 0)
        file = 0;
#else
    FILE* file = fopen(temporary_path.c_str(), "wbx");
#endif
    if (!file)
        error = std::string("cannot exclusively create temporary output: ") + errno_message();
    return file;
}

bool commit_temporary_output(const path_t& output_path, std::string& error)
{
    const path_t temporary_path = output_temporary_path(output_path);
    if (!replace_path(temporary_path, output_path, error))
    {
        discard_temporary_output(output_path);
        return false;
    }
    return true;
}

void discard_temporary_output(const path_t& output_path)
{
    remove_path(output_temporary_path(output_path));
}
