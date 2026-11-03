#include "common.h"
#include <string.h>
#include <algorithm>
#include "core/StringUtil.h"
#include "fat/Directory.h"
#include "FileInfo.h"
#include "RecursiveSdFolderFactory.h"
#include "SearchMatcher.h"

SharedPtr<SdFolder> RecursiveSdFolderFactory::CreateFromPathRecursive(
    const char* basePath, const char* searchQuery, const vu8& cancelRequested,
    SdFolderSortDirection direction) const
{
    // Keep directory state on the heap: the IO thread has only a 2 KB stack.
    // One open directory per depth also bounds memory regardless of folder breadth.
    struct Frame
    {
        TCHAR path[256];
        DIR directory {};
        bool open = false;

        ~Frame()
        {
            if (open)
                f_closedir(&directory);
        }
    };

    auto frames = std::make_unique<Frame[]>(MAX_DEPTH + 1);
    auto sdFileInfo = std::make_unique<FILINFO>();
    auto cwd = std::make_unique<TCHAR[]>(256);
    if (f_getcwd(cwd.get(), 256) != FR_OK)
        return nullptr;
    const size_t cwdLength = strlen(cwd.get());
    auto files = (FileInfo**)malloc(sizeof(FileInfo*) * MAX_RESULTS);
    if (!files)
        return nullptr;
    int count = 0;
    auto candidates = std::make_unique<SearchCandidate[]>(MAX_RESULTS);
    SearchMatcher matcher(searchQuery);
    const SearchCandidateLess less { direction };
    int depth = 0;
    bool canceled = false;

    StringUtil::Copy(frames[0].path, basePath, sizeof(frames[0].path));
    frames[0].open = f_opendir(&frames[0].directory, frames[0].path) == FR_OK;
    if (!frames[0].open)
        depth = -1;

    while (depth >= 0)
    {
        if (cancelRequested)
        {
            canceled = true;
            break;
        }

        auto& current = frames[depth];
        if (f_readdir(&current.directory, sdFileInfo.get()) != FR_OK || !sdFileInfo->fname[0])
        {
            f_closedir(&current.directory);
            current.open = false;
            depth--;
            continue;
        }

        if (sdFileInfo->fname[0] == '.' || (sdFileInfo->fattrib & AM_HID))
            continue;

        if (sdFileInfo->fattrib & AM_DIR)
        {
            if (depth == MAX_DEPTH)
                continue;
            auto& next = frames[depth + 1];
            if (strcmp(current.path, ".") == 0)
                next.path[0] = 0;
            else
            {
                StringUtil::Copy(next.path, current.path, sizeof(next.path));
                if (strlcat(next.path, "/", sizeof(next.path)) >= sizeof(next.path))
                    continue;
            }
            if (strlcat(next.path, sdFileInfo->fname, sizeof(next.path)) >= sizeof(next.path))
                continue;
            next.open = f_opendir(&next.directory, next.path) == FR_OK;
            if (next.open)
                depth++;
            continue;
        }

        auto fileType = _fileTypeProvider->GetFileType(sdFileInfo->fname);
        if (fileType->GetClassification() == FileTypeClassification::Unknown)
            continue;
        const int score = matcher.Match(sdFileInfo->fname);
        if (score < 0)
            continue;

        const bool isCurrentFolder = strcmp(current.path, ".") == 0;
        const size_t parentLength = isCurrentFolder ? 0 : strlen(current.path) + 1;
        const size_t separatorLength = cwdLength && cwd[cwdLength - 1] == '/' ? 0 : 1;
        if (cwdLength + separatorLength + parentLength + strlen(sdFileInfo->fname) >= 256)
            continue;
        if (count == MAX_RESULTS && score > candidates[0].score)
            continue;

        auto file = std::make_unique<FileInfo>(sdFileInfo->fname, fileType,
            FastFileRef(&current.directory, sdFileInfo.get()), sdFileInfo->fattrib,
            isCurrentFolder ? nullptr : current.path);
        const SearchCandidate candidate { file.get(), score };
        if (count < MAX_RESULTS)
        {
            candidates[count++] = candidate;
            file.release();
            std::push_heap(candidates.get(), candidates.get() + count, less);
        }
        else if (less(candidate, candidates[0]))
        {
            std::pop_heap(candidates.get(), candidates.get() + count, less);
            delete candidates[count - 1].file;
            candidates[count - 1] = candidate;
            file.release();
            std::push_heap(candidates.get(), candidates.get() + count, less);
        }
    }

    for (int i = 0; i < count; i++)
        files[i] = const_cast<FileInfo*>(candidates[i].file);
    // The folder owns all collected files even when cancellation discards them.
    auto result = SharedPtr<SdFolder>::MakeShared(files, count);
    return canceled ? nullptr : result;
}
