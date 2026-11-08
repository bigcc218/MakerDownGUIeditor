// MDGE Studio - native project container (.mdge)
#pragma once

#include "model.h"

#include <cstdint>
#include <string>

namespace mdge {

constexpr std::uint16_t kProjectFormatVersionMajor = 1;
constexpr std::uint16_t kProjectFormatVersionMinor = 1;
constexpr char kProjectExtension[] = "mdge";
constexpr std::uint64_t kProjectMaxFileBytes = 64ull * 1024ull * 1024ull;

bool SaveProjectFile(const std::wstring& path, const Document& document,
                     std::wstring& error);
bool LoadProjectFile(const std::wstring& path, Document& document,
                     std::wstring& error);

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);

}  // namespace mdge
