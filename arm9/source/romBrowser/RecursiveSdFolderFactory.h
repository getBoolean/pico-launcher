#pragma once
#include <memory>
#include "SdFolder.h"
#include "FileType/IFileTypeProvider.h"

class RecursiveSdFolderFactory
{
public:
    RecursiveSdFolderFactory(const IFileTypeProvider* fileTypeProvider)
        : _fileTypeProvider(fileTypeProvider) { }

    std::unique_ptr<SdFolder> CreateFromPathRecursive(
        const char* basePath, const char* searchQuery) const;

private:
    static constexpr int MAX_DEPTH = 8;
    static constexpr int MAX_RESULTS = 256;

    const IFileTypeProvider* _fileTypeProvider;
};
