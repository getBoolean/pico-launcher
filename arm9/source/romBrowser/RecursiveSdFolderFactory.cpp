#include "common.h"
#include <string.h>
#include <ctype.h>
#include "core/StringUtil.h"
#include "fat/Directory.h"
#include "FileInfo.h"
#include "RecursiveSdFolderFactory.h"

static bool CaseInsensitiveContains(const char* haystack, const char* needle)
{
    if (!needle[0])
        return true;
    for (; *haystack; haystack++)
    {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n))
        {
            h++;
            n++;
        }
        if (!*n)
            return true;
    }
    return false;
}

SharedPtr<SdFolder> RecursiveSdFolderFactory::CreateFromPathRecursive(
    const char* basePath, const char* searchQuery, const vu8& cancelRequested) const
{
    TCHAR savedCwd[256];
    f_getcwd(savedCwd, sizeof(savedCwd) / sizeof(savedCwd[0]));

    if (f_chdir(basePath) != FR_OK)
    {
        f_chdir(savedCwd);
        auto empty = (FileInfo**)malloc(sizeof(FileInfo*) * 1);
        return SharedPtr<SdFolder>::MakeShared(empty, 0);
    }

    int count = 0;
    int bufferSize = 8;
    auto fileInfos = (FileInfo**)malloc(sizeof(FileInfo*) * bufferSize);

    // Explicit stack for iterative depth-first traversal
    // Each entry is a relative path to explore
    struct DirEntry
    {
        TCHAR path[256];
        int depth;
    };

    int stackSize = 0;
    int stackCapacity = 8;
    auto stack = (DirEntry*)malloc(sizeof(DirEntry) * stackCapacity);

    // Push the root directory
    StringUtil::Copy(stack[0].path, ".", sizeof(stack[0].path) / sizeof(stack[0].path[0]));
    stack[0].depth = 0;
    stackSize = 1;

    auto sdFileInfo = std::make_unique<FILINFO>();

    bool canceled = false;
    while (!canceled && stackSize > 0 && count < MAX_RESULTS)
    {
        // Pop
        stackSize--;
        DirEntry current = stack[stackSize];

        Directory directory;
        if (directory.Open(current.path) != FR_OK)
            continue;

        while (count < MAX_RESULTS)
        {
            if (cancelRequested)
            {
                canceled = true;
                break;
            }

            if (directory.Read(sdFileInfo.get()) != FR_OK)
                break;

            if (sdFileInfo->fname[0] == 0)
                break;

            bool isHidden = sdFileInfo->fname[0] == '.' || (sdFileInfo->fattrib & AM_HID);

            if (isHidden)
                continue;

            if (sdFileInfo->fattrib & AM_DIR)
            {
                // Push sub-directory if within depth limit
                if (current.depth < MAX_DEPTH)
                {
                    if (stackSize >= stackCapacity)
                    {
                        stackCapacity *= 2;
                        stack = (DirEntry*)realloc(stack, sizeof(DirEntry) * stackCapacity);
                    }
                    DirEntry& entry = stack[stackSize];
                    if (strcmp(current.path, ".") == 0)
                    {
                        StringUtil::Copy(entry.path, sdFileInfo->fname,
                            sizeof(entry.path) / sizeof(entry.path[0]));
                    }
                    else
                    {
                        StringUtil::Copy(entry.path, current.path,
                            sizeof(entry.path) / sizeof(entry.path[0]));
                        strlcat(entry.path, "/", sizeof(entry.path));
                        strlcat(entry.path, sdFileInfo->fname, sizeof(entry.path));
                    }
                    entry.depth = current.depth + 1;
                    stackSize++;
                }
            }
            else
            {
                auto fileType = _fileTypeProvider->GetFileType(sdFileInfo->fname);
                if (fileType->GetClassification() == FileTypeClassification::Unknown)
                    continue;

                if (!CaseInsensitiveContains(sdFileInfo->fname, searchQuery))
                    continue;

                if (count >= bufferSize)
                {
                    bufferSize *= 2;
                    fileInfos = (FileInfo**)realloc(fileInfos, sizeof(FileInfo*) * bufferSize);
                }
                fileInfos[count++] = new FileInfo(sdFileInfo->fname, fileType,
                    FastFileRef(directory.GetFatFsDirectory(), sdFileInfo.get()),
                    sdFileInfo->fattrib,
                    strcmp(current.path, ".") == 0 ? nullptr : current.path);
            }
        }
    }

    free(stack);
    f_chdir(savedCwd);

    // Constructing the folder first ensures the found files are freed when canceled
    auto result = SharedPtr<SdFolder>::MakeShared(fileInfos, count);
    if (canceled)
        return nullptr;
    return result;
}
