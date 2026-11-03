#include "common.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include "fat/CurrentDirectoryPath.h"

static std::string cwd;
static FRESULT failure = FR_OK;
static std::vector<u32> capacities;

FRESULT f_getcwd(TCHAR* buffer, u32 capacity)
{
    capacities.push_back(capacity);
    buffer[0] = 0;
    if (failure != FR_OK)
        return failure;
    if (capacity <= cwd.size())
        return FR_NOT_ENOUGH_CORE;
    std::memcpy(buffer, cwd.c_str(), cwd.size() + 1);
    return FR_OK;
}

int main()
{
    for (const auto& path : {std::string("/"), "/" + std::string(254, 'a'),
            "/" + std::string(255, 'b'), "/" + std::string(700, 'c'),
            "/games/" + std::string(250, 'd') + "\xE4\xBF\x9D\xE5\xAD\x98"})
    {
        cwd = path;
        capacities.clear();
        auto snapshot = ReadCurrentDirectoryPath();
        assert(snapshot && path == snapshot.get());
        assert(capacities.front() == 256);
        assert(capacities.back() > path.size());
        cwd = "/changed";
        assert(path == snapshot.get());
        auto next = ReadCurrentDirectoryPath();
        assert(next && cwd == next.get());
        assert(path == snapshot.get());
    }

    failure = FR_DISK_ERR;
    capacities.clear();
    assert(!ReadCurrentDirectoryPath());
    assert(capacities.size() == 1);
    failure = FR_OK;
    cwd = "/recovered";
    auto recovered = ReadCurrentDirectoryPath();
    assert(recovered && cwd == recovered.get());
}
