// MDGE Studio - small Windows file helpers
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mdge {

std::wstring Win32ErrorText(unsigned long errorCode);
std::wstring LastWin32ErrorText();

bool ReadFileBytes(const std::wstring& path, std::vector<std::uint8_t>& bytes,
                   std::wstring& error, std::uint64_t maxSize = 256ull * 1024ull * 1024ull);
bool WriteFileBytesAtomic(const std::wstring& path,
                          const std::vector<std::uint8_t>& bytes,
                          std::wstring& error);
bool WriteUtf8TextFile(const std::wstring& path, const std::string& utf8,
                       std::wstring& error);
bool EnsureDirectoryExists(const std::wstring& directory, std::wstring& error);

}  // namespace mdge
