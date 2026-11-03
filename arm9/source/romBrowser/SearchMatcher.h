#pragma once
#include <memory>
#include "SdFolderSortDirection.h"

class FileInfo;

struct SearchCandidate
{
    const FileInfo* file;
    int score;
};

/// Orders cached matches by relevance, then the requested name direction.
struct SearchCandidateLess
{
    SdFolderSortDirection direction = SdFolderSortDirection::Ascending;
    bool operator()(const SearchCandidate& a, const SearchCandidate& b) const;
};

/// A scoring pass owns its workspace. The query must outlive the pass.
class SearchMatcher
{
    struct Workspace;
    std::unique_ptr<Workspace> _workspace;
    const char* _query;
    unsigned _queryLength = 0;

public:
    explicit SearchMatcher(const char* query);
    ~SearchMatcher();

    /// Returns -1 for no match, 0 for a literal match, or the typo distance.
    int Match(const char* fileName);
};
