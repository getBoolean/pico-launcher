#include "common.h"
#include <string.h>
#include "core/StringUtil.h"
#include "FileInfo.h"

static std::unique_ptr<TCHAR[]> CopyString(const TCHAR* string)
{
    if (!string)
        return nullptr;
    u32 bufferLength = strlen(string) + 1;
    auto copy = std::make_unique_for_overwrite<TCHAR[]>(bufferLength);
    StringUtil::Copy(copy.get(), string, bufferLength);
    return copy;
}

FileInfo::FileInfo(const FileInfo& fileInfo)
    : _name(CopyString(fileInfo.GetFileName())), _parentPath(CopyString(fileInfo.GetParentPath()))
    , _type(fileInfo._type), _fastFileRef(fileInfo._fastFileRef), _attributes(fileInfo._attributes) { }

FileInfo::FileInfo(const TCHAR* fileName, const FileType* type, const FastFileRef& fastFileRef, u8 attributes,
    const TCHAR* parentPath)
    : _name(CopyString(fileName)), _parentPath(CopyString(parentPath))
    , _type(type), _fastFileRef(fastFileRef), _attributes(attributes) { }
