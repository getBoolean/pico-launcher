#include "common.h"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include "FileInfo.h"
#include "SearchMatcher.h"

namespace
{
    constexpr unsigned MAX_QUERY_LENGTH = 31;

    unsigned FoldAscii(unsigned c)
    {
        return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }

    unsigned ReadCharacter(const char*& cursor, const char* end)
    {
        unsigned first = static_cast<unsigned char>(*cursor++);
        unsigned extra = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC0 ? 1 : 0;
        if (!extra || end - cursor < extra)
            return FoldAscii(first);
        unsigned value = first & (0x7F >> (extra + 1));
        for (unsigned i = 0; i < extra; i++)
        {
            unsigned c = static_cast<unsigned char>(cursor[i]);
            if ((c & 0xC0) != 0x80)
                return first;
            value = (value << 6) | (c & 0x3F);
        }
        cursor += extra;
        return value;
    }

    bool ContainsLiteral(const char* name, const char* query)
    {
        if (!*query)
            return true;
        for (; *name; name++)
        {
            const char* n = name;
            const char* q = query;
            while (*n && *q && FoldAscii(static_cast<unsigned char>(*n)) == FoldAscii(static_cast<unsigned char>(*q)))
            {
                n++;
                q++;
            }
            if (!*q)
                return true;
        }
        return false;
    }
}

struct SearchMatcher::Workspace
{
    unsigned query[MAX_QUERY_LENGTH];
    u8 rows[3][MAX_QUERY_LENGTH + 1];
};

SearchMatcher::SearchMatcher(const char* query)
    : _workspace(std::make_unique<Workspace>()), _query(query ? query : "")
{
    const char* cursor = _query;
    const char* end = cursor + strlen(cursor);
    while (cursor < end && _queryLength < MAX_QUERY_LENGTH)
        _workspace->query[_queryLength++] = ReadCharacter(cursor, end);
    if (cursor < end)
        _queryLength = MAX_QUERY_LENGTH + 1;
}

SearchMatcher::~SearchMatcher() = default;

int SearchMatcher::Match(const char* fileName)
{
    if (ContainsLiteral(fileName, _query))
        return 0;
    if (_queryLength < 3 || _queryLength > MAX_QUERY_LENGTH)
        return -1;

    const unsigned budget = _queryLength < 6 ? 1 : 2;
    auto& workspace = *_workspace;
    u8* previous = workspace.rows[0];
    u8* current = workspace.rows[1];
    u8* beforePrevious = workspace.rows[2];
    for (unsigned j = 0; j <= _queryLength; j++)
        previous[j] = j;

    const char* cursor = fileName;
    const char* end = strrchr(fileName, '.');
    if (!end)
        end = fileName + strlen(fileName);
    unsigned previousCharacter = 0;
    bool hasPreviousCharacter = false;
    unsigned best = _queryLength;
    while (cursor < end)
    {
        unsigned character = ReadCharacter(cursor, end);
        current[0] = 0; // A candidate substring may start at any character.
        for (unsigned j = 1; j <= _queryLength; j++)
        {
            unsigned distance = std::min({
                unsigned(previous[j]) + 1,
                unsigned(current[j - 1]) + 1,
                unsigned(previous[j - 1]) + (character != workspace.query[j - 1])});
            if (hasPreviousCharacter && j > 1 && character == workspace.query[j - 2] &&
                previousCharacter == workspace.query[j - 1])
                distance = std::min(distance, unsigned(beforePrevious[j - 2]) + 1);
            current[j] = distance;
        }
        best = std::min(best, unsigned(current[_queryLength]));
        auto spare = beforePrevious;
        beforePrevious = previous;
        previous = current;
        current = spare;
        previousCharacter = character;
        hasPreviousCharacter = true;
    }
    return best <= budget ? int(best) : -1;
}

bool SearchCandidateLess::operator()(const SearchCandidate& a, const SearchCandidate& b) const
{
    if (a.score != b.score)
        return a.score < b.score;
    int nameOrder = strcasecmp(a.file->GetFileName(), b.file->GetFileName());
    if (nameOrder)
        return direction == SdFolderSortDirection::Ascending ? nameOrder < 0 : nameOrder > 0;
    const char* aParent = a.file->GetParentPath();
    const char* bParent = b.file->GetParentPath();
    int parentOrder = strcmp(aParent ? aParent : "", bParent ? bParent : "");
    if (parentOrder)
        return parentOrder < 0;
    return strcmp(a.file->GetFileName(), b.file->GetFileName()) < 0;
}
