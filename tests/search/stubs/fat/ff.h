#pragma once
#include "FastFileRef.h"
#include <string>
struct DIR { std::string path; size_t index = 0; };
struct FILINFO { char fname[256]; u8 fattrib; };
using FRESULT = int;
constexpr FRESULT FR_OK = 0, FR_DISK_ERR = 1;
FRESULT f_getcwd(TCHAR*, u32);
FRESULT f_opendir(DIR*, const TCHAR*);
FRESULT f_closedir(DIR*);
FRESULT f_readdir(DIR*, FILINFO*);
inline FRESULT f_rewinddir(DIR* dir) { dir->index = 0; return FR_OK; }
