#pragma once
#include <cstdint>
#include <cstdlib>
#include <memory>
using u8 = std::uint8_t;
using vu8 = volatile u8;
using u32 = std::uint32_t;
using vu32 = volatile u32;
size_t strlcat(char*, const char*, size_t);
