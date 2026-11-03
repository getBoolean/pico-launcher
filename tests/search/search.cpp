#include "common.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <strings.h>
#include "SdFolder.h"
#include "SearchMatcher.h"

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
    {
        auto fuzzyFiles = static_cast<FileInfo**>(malloc(3 * sizeof(FileInfo*)));
        fuzzyFiles[0] = new FileInfo("game.nds", &game, {}, 0);
        fuzzyFiles[1] = new FileInfo("gmae demo.nds", &game, {}, 0);
        fuzzyFiles[2] = new FileInfo("unrelated.nds", &game, {}, 0);
        SdFolder fuzzyContents(fuzzyFiles, 3);
        params.searchQuery = "gmae";
        for (auto direction : {SdFolderSortDirection::Ascending, SdFolderSortDirection::Descending})
        {
            params.sortDirection = direction;
            u32 count;
            auto results = fuzzyContents.FilterAndSort(params, count);
            assert(count == 2);
            assert(!strcmp(results[0]->GetFileName(), "gmae demo.nds"));
            assert(!strcmp(results[1]->GetFileName(), "game.nds"));
        }
    }
    const struct { const char* query; const char* name; int score; } matchingCases[] = {
        { "gmae", "game.nds", 1 },
        { "game", "GAME demo.nds", 0 },
        { "ame", "game.nds", 0 },
        { "game", "gme.nds", 1 },
        { "game", "gamme.nds", 1 },
        { "game", "gome.nds", 1 },
        { "ct", "cat.nds", -1 },
        { "cat", "cot.nds", 1 },
        { "abcde", "abxde.nds", 1 },
        { "abcde", "abxye.nds", -1 },
        { "abcdef", "abxyef.nds", 2 },
        { "abcdef", "axyzef.nds", -1 },
        { "abcdefghijklmnopqrstuvwxyz12345", "abxdefghijxlmnopqrstuvwxyz12345.nds", 2 },
        { "abcdefghijklmnopqrstuvwxyz12345", "abxdefghijxlmnopqrxtuvwxyz12345.nds", -1 },
        { "aaaa", "aaab.nds", 1 },
        { "nds", "game.nds", 0 },
        { "ndx", "game.nds", -1 },
        { "space-dash ", "Space-dash One.nds", 0 },
        { "éé", "éx.nds", -1 },
        { "abé", "abè.nds", 1 },
        { "É", "é.nds", -1 },
        { "😀ab", "😀ac.nds", 1 },
        { "遊戲", "我的遊戲.nds", 0 },
        { "query", "", -1 },
        { "", "game.nds", 0 },
    };
    for (const auto& test : matchingCases)
    {
        SearchMatcher matcher(test.query);
        int score = matcher.Match(test.name);
        if (score != test.score)
            fprintf(stderr, "query '%s', name '%s': expected %d, got %d\n", test.query, test.name, test.score, score);
        assert(score == test.score);
        assert(matcher.Match(test.name) == score); // Reusing scratch keeps the same result.
    }
    puts("Search filtering and duplicate-name sorting passed");
}
