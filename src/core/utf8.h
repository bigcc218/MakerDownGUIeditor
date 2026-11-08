// MDGE Studio - explicit UTF-8 conversion helpers
#pragma once

#include <string>
#include <string_view>

namespace mdge {

bool Utf8Encode(std::wstring_view text, std::string& output,
                std::wstring* error = nullptr);
bool Utf8Decode(std::string_view bytes, std::wstring& output,
                std::wstring* error = nullptr);

std::string Utf8Encode(std::wstring_view text);
std::wstring Utf8Decode(std::string_view bytes);

}  // namespace mdge
