#include "common.h"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "fat/ff.h"
#include "fat/CurrentDirectoryPath.h"
#include "RecursiveSdFolderFactory.h"

extern "C" void shared_ptr_increase_ref_count(vu32& count) { count = count + 1; }
extern "C" size_t strlcat(char* dest, const char* source, size_t size) noexcept
{
    size_t used = strnlen(dest, size), length = strlen(source);
    if (used < size) { strncat(dest, source, size - used - 1); }
    return used + length;
}
struct Entry { std::string name; u8 attributes = 0; };
static std::map<std::string, std::vector<Entry>> tree;
static std::set<DIR*> openDirectories;
static vu8 canceled;
static int reads, cancelAt, failAt;
static bool cwdAvailable = true;
static std::string cwd = "/games";
std::unique_ptr<char[]> ReadCurrentDirectoryPath()
{
    if (!cwdAvailable) return nullptr;
    auto path = std::make_unique<char[]>(cwd.size() + 1);
    strcpy(path.get(), cwd.c_str());
    return path;
}
FRESULT f_opendir(DIR* dir, const TCHAR* path)
{
    if (!tree.contains(path)) return FR_DISK_ERR;
    assert(openDirectories.insert(dir).second);
    assert(openDirectories.size() <= RecursiveSdFolderFactory::MAX_DEPTH + 1);
    dir->path = path; dir->index = 0;
    return FR_OK;
}
FRESULT f_closedir(DIR* dir)
{
    assert(openDirectories.erase(dir) == 1);
    return FR_OK;
}
FRESULT f_readdir(DIR* dir, FILINFO* info)
{
    assert(openDirectories.contains(dir));
    if (++reads == cancelAt) canceled = 1;
    if (reads == failAt) return FR_DISK_ERR;
    const auto& entries = tree.at(dir->path);
    info->fname[0] = 0;
    if (dir->index < entries.size())
    {
        const auto& entry = entries[dir->index++];
        assert(entry.name.size() < sizeof(info->fname));
        strcpy(info->fname, entry.name.c_str()); info->fattrib = entry.attributes;
    }
    return FR_OK;
}
struct Provider : IFileTypeProvider
{
    FileType game {FileTypeClassification::Game}, unknown {FileTypeClassification::Unknown};
    const FileType* GetFileType(const TCHAR* path) const override
    {
        return std::string(path).ends_with(".nds") ? &game : &unknown;
    }
};
static Provider provider;
static RecursiveSdFolderFactory factory(&provider);
static void reset()
{
    assert(openDirectories.empty());
    tree = {{".", {}}}; reads = cancelAt = failAt = 0; canceled = 0;
    cwdAvailable = true; cwd = "/games";
}
static auto search(const char* query = "game")
{
    auto result = factory.CreateFromPathRecursive(".", query, canceled);
    assert(openDirectories.empty());
    return result;
}
int main()
{
    reset();
    tree["."] = {{"A", AM_DIR}, {"B", AM_DIR}, {".hidden", AM_DIR},
                 {"hidden", AM_DIR | AM_HID}, {"GAME.nds"}, {"game.txt"}, {".game.nds"}, {"game-hidden.nds", AM_HID}};
    tree["A"] = {{"game.nds"}}; tree["B"] = {{"game.nds"}};
    tree[".hidden"] = {{"game.nds"}}; tree["hidden"] = {{"game.nds"}};
    auto result = search("GaMe");
    assert(result && result->GetFileCount() == 3);
    std::set<std::string> parents;
    for (int i = 0; i < 3; ++i)
    {
        const char* parent = result->GetFiles()[i]->GetParentPath();
        parents.insert(parent ? parent : ".");
    }
    assert((parents == std::set<std::string>{".", "A", "B"}));

    reset();
    std::string path = ".";
    for (int depth = 0; depth <= RecursiveSdFolderFactory::MAX_DEPTH + 1; ++depth)
    {
        tree[path].push_back({"game.nds"}); tree[path].push_back({"d", AM_DIR});
        path = path == "." ? "d" : path + "/d";
    }
    assert(search()->GetFileCount() == RecursiveSdFolderFactory::MAX_DEPTH + 1);

    reset();
    for (int i = 0; i < RecursiveSdFolderFactory::MAX_RESULTS + 20; ++i)
        tree["."].push_back({"game" + std::to_string(i) + ".nds"});
    assert(search()->GetFileCount() == RecursiveSdFolderFactory::MAX_RESULTS);

    reset();
    for (int i = 0; i < RecursiveSdFolderFactory::MAX_RESULTS + 20; ++i)
        tree["."].push_back({"gmae" + std::to_string(i) + ".nds"});
    tree["."].push_back({"game-late.nds"});
    result = search();
    assert(result->GetFileCount() == RecursiveSdFolderFactory::MAX_RESULTS);
    SdFolderFilterSortParams ranked;
    ranked.searchQuery = "game";
    u32 rankedCount;
    auto rankedFiles = result->FilterAndSort(ranked, rankedCount);
    assert(rankedCount == RecursiveSdFolderFactory::MAX_RESULTS);
    assert(!strcmp(rankedFiles[0]->GetFileName(), "game-late.nds"));
    for (auto direction : {SdFolderSortDirection::Ascending, SdFolderSortDirection::Descending})
    {
        reset();
        std::vector<std::string> expected;
        for (int i = 0; i < RecursiveSdFolderFactory::MAX_RESULTS + 20; ++i)
        {
            auto name = "game" + std::to_string(i) + ".nds";
            tree["."].push_back({name});
            expected.push_back(name);
        }
        std::sort(expected.begin(), expected.end());
        if (direction == SdFolderSortDirection::Descending)
            std::reverse(expected.begin(), expected.end());
        result = factory.CreateFromPathRecursive(".", "game", canceled, direction);
        ranked.sortDirection = direction;
        rankedFiles = result->FilterAndSort(ranked, rankedCount);
        assert(rankedCount == RecursiveSdFolderFactory::MAX_RESULTS);
        for (u32 i = 0; i < rankedCount; ++i)
            assert(expected[i] == rankedFiles[i]->GetFileName());
        assert(openDirectories.empty());
        cancelAt = 270; reads = 0;
        assert(!factory.CreateFromPathRecursive(".", "game", canceled, direction));
        assert(openDirectories.empty());
    }

    reset(); tree["."] = {{"A", AM_DIR}}; tree["A"] = {{"game.nds"}};
    cancelAt = 2; assert(!search());
    reset(); canceled = 1; assert(!search());
    reset(); tree["."] = {{"A", AM_DIR}, {"game.nds"}};
    // An unreadable child must not prevent searching its siblings.
    assert(search()->GetFileCount() == 1);
    reset(); tree["."] = {{"game.nds"}}; failAt = 1;
    assert(search()->GetFileCount() == 0);
    reset(); cwdAvailable = false; assert(!search());
    reset(); tree.clear(); assert(search()->GetFileCount() == 0);
    reset(); cwd = "/" + std::string(245, 'x'); tree["."] = {{"game.nds"}};
    assert(search()->GetFileCount() == 1); // Complete path has 255 bytes.
    cwd += 'x';
    assert(search()->GetFileCount() == 0);
    cwd = "/" + std::string(300, 'x');
    result = search();
    assert(result && result->GetFileCount() == 0);
    reset(); tree["."] = {{"A", AM_DIR}}; tree["A"] = {{"game.nds"}};
    assert(search("absent")->GetFileCount() == 0);
    puts("Recursive search bounds, cancellation, visibility and FAT failures passed");
}
