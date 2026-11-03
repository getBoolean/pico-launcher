#pragma once
#include "romBrowser/FileType/FileTypeClassification.h"
class InternalFileInfo;
class FileType
{
    FileTypeClassification _classification;
public:
    explicit FileType(FileTypeClassification classification) : _classification(classification) { }
    FileTypeClassification GetClassification() const { return _classification; }
    InternalFileInfo* CreateInternalFileInfo(const class FastFileRef&) const { return nullptr; }
};
