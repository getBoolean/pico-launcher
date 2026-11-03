#pragma once
#include <memory>

/// @brief Reads an owned UTF-8 snapshot of the actual current directory.
/// @return The complete path, or null when the filesystem or allocation fails.
std::unique_ptr<char[]> ReadCurrentDirectoryPath();
