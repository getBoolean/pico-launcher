#pragma once
using TCHAR = char;
constexpr u8 AM_RDO = 1, AM_HID = 2, AM_SYS = 4;
class FastFileRef
{
public:
    u32 GetFileSize() const { return 0; }
};
