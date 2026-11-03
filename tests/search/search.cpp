#include "common.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <strings.h>
#include "SdFolder.h"

int main()
{
    const FileType game(FileTypeClassification::Game);
    const FileType folder(FileTypeClassification::Folder);
    const FileType unknown(FileTypeClassification::Unknown);
    auto files = static_cast<FileInfo**>(malloc(36 * sizeof(FileInfo*)));
    for (int i = 0; i < 32; i++)
        files[i] = new FileInfo(i % 2 ? "game.nds" : "GAME.nds", &game, {}, 0,
            ("parent-" + std::to_string(i)).c_str());
    files[32] = new FileInfo("Space-dash One.nds", &game, {}, 0);
    files[33] = new FileInfo("game folder", &folder, {}, 0);
    files[34] = new FileInfo(".game.nds", &game, {}, AM_HID);
    files[35] = new FileInfo("game.xyz", &unknown, {}, 0);
    SdFolder contents(files, 36);
    SdFolderFilterSortParams params;
    for (auto direction : {SdFolderSortDirection::Ascending, SdFolderSortDirection::Descending})
    {
        params.sortDirection = direction;
        params.searchQuery = "GaMe";
        u32 count;
        auto results = contents.FilterAndSort(params, count);
        assert(count == 32);
        std::set<std::string> parents;
        for (u32 i = 0; i < count; i++) parents.insert(results[i]->GetParentPath());
        assert(parents.size() == 32);
        params.searchQuery = "space-dash ";
        results = contents.FilterAndSort(params, count);
        assert(count == 1 && !strcmp(results[0]->GetFileName(), "Space-dash One.nds"));
        params.searchQuery = "no-match";
        results = contents.FilterAndSort(params, count);
        assert(count == 0);
        params.searchQuery = "";
        results = contents.FilterAndSort(params, count);
        assert(count == 34 && results[0]->GetFileType() == &folder);
        for (u32 i = 2; i < count; i++)
        {
            int comparison = strcasecmp(results[i - 1]->GetFileName(), results[i]->GetFileName());
            assert(direction == SdFolderSortDirection::Ascending ? comparison <= 0 : comparison >= 0);
        }
    }
    puts("Search filtering and duplicate-name sorting passed");
}
