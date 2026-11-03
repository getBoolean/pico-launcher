#pragma once
using TCHAR = char;
constexpr u8 AM_RDO = 1, AM_HID = 2, AM_SYS = 4, AM_DIR = 16;
class FastFileRef
{
public:
    FastFileRef() = default;
    FastFileRef(const void*, const void*) { }
    u32 GetFileSize() const { return 0; }
};
