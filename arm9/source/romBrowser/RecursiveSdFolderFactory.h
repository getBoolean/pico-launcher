#pragma once
#include "core/SharedPtr.h"
#include "SdFolder.h"
#include "FileType/IFileTypeProvider.h"

class RecursiveSdFolderFactory
{
public:
    RecursiveSdFolderFactory(const IFileTypeProvider* fileTypeProvider)
        : _fileTypeProvider(fileTypeProvider) { }

    /// @brief Searches basePath and its subfolders for the best filename matches.
    /// @param basePath The folder to start searching in.
    /// @param searchQuery The case insensitive text to search for.
    /// @param cancelRequested Set when the search should stop early.
    /// @return A folder with the matching files, or null when canceled.
    SharedPtr<SdFolder> CreateFromPathRecursive(
        const char* basePath, const char* searchQuery, const vu8& cancelRequested,
        SdFolderSortDirection direction = SdFolderSortDirection::Ascending) const;

    static constexpr int MAX_DEPTH = 8;
    static constexpr int MAX_RESULTS = 256;

private:
    const IFileTypeProvider* _fileTypeProvider;
};
