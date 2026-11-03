#include "common.h"
#include <limits>
#include <memory>
#include <new>
#include "CurrentDirectoryPath.h"

std::unique_ptr<char[]> ReadCurrentDirectoryPath()
{
    static_assert(FF_LFN_UNICODE == 2, "Current directory snapshots use UTF-8");
    // With stack-backed LFN work buffers, f_getcwd reports this error only
    // when its output does not fit. Heap-backed LFN would make it ambiguous.
    static_assert(FF_USE_LFN == 2, "Review f_getcwd growth if LFN allocation changes");
    for (u32 capacity = 256;; capacity *= 2)
    {
        auto path = std::unique_ptr<char[]>(new (std::nothrow) char[capacity]);
        if (!path)
            return nullptr;
        const auto result = f_getcwd(path.get(), capacity);
        if (result == FR_OK)
            return path;
        if (result != FR_NOT_ENOUGH_CORE || capacity > std::numeric_limits<u32>::max() / 2)
            return nullptr;
    }
}
