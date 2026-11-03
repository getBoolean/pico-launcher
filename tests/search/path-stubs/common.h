#pragma once
#include <cstdint>
using u32 = std::uint32_t;
using TCHAR = char;
using FRESULT = int;
constexpr FRESULT FR_OK = 0, FR_DISK_ERR = 1, FR_NOT_ENOUGH_CORE = 17;
#define FF_USE_LFN 2
#define FF_LFN_UNICODE 2
FRESULT f_getcwd(TCHAR*, u32);
