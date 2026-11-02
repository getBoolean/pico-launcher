#pragma once
#include "fat/ff.h"
#include "fat/FastFileRef.h"
#include "FileType/FileType.h"

class InternalFileInfo;

class FileInfo
{
public:
    FileInfo() { }
    FileInfo(const FileInfo& fileInfo);
    /// @param parentPath Folder containing the file, relative to the current folder,
    ///                   or null when the file is in the current folder.
    FileInfo(const TCHAR* fileName, const FileType* type, const FastFileRef& fastFileRef, u8 attributes,
        const TCHAR* parentPath = nullptr);

    FileInfo &operator=(FileInfo&& rhs)
    {
        if (this != &rhs)
        {
            _name = std::move(rhs._name);
            _parentPath = std::move(rhs._parentPath);
            _type = rhs._type;
            _fastFileRef = rhs._fastFileRef;
            _attributes = rhs._attributes;
        }

        return *this;
    }

    const TCHAR* GetFileName() const { return _name.get(); }

    /// @brief Gets the folder containing the file, relative to the current folder.
    /// @return The parent path, or null when the file is in the current folder.
    const TCHAR* GetParentPath() const { return _parentPath.get(); }
    const FileType* GetFileType() const { return _type; }
    u32 GetFileSize() const { return _fastFileRef.GetFileSize(); }

    InternalFileInfo* CreateInternalFileInfo() const
    {
        return _type->CreateInternalFileInfo(_fastFileRef);
    }

    const FastFileRef& GetFastFileRef() const { return _fastFileRef; }

    bool IsReadOnly() const { return _attributes & AM_RDO; }
    bool IsHidden() const { return _attributes & AM_HID; }
    bool IsSystem() const { return _attributes & AM_SYS; }

private:
    std::unique_ptr<TCHAR[]> _name;
    std::unique_ptr<TCHAR[]> _parentPath;
    const FileType* _type;
    FastFileRef _fastFileRef;
    u8 _attributes;
};
