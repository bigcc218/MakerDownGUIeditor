#include "utf8.h"

#include <cstdint>

namespace mdge {
namespace {

void SetError(std::wstring* error, const wchar_t* message) {
    if (error) {
        *error = message;
    }
}

}  // namespace

bool Utf8Encode(std::wstring_view text, std::string& output,
                std::wstring* error) {
    output.clear();
    output.reserve(text.size() * 3);
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(text[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 >= text.size()) {
                SetError(error, L"文本包含孤立的 UTF-16 高代理项。");
                output.clear();
                return false;
            }
            const std::uint32_t low = static_cast<std::uint32_t>(text[i + 1]);
            if (low < 0xDC00 || low > 0xDFFF) {
                SetError(error, L"文本包含无效的 UTF-16 代理项对。");
                output.clear();
                return false;
            }
            cp = 0x10000u + ((cp - 0xD800u) << 10u) + (low - 0xDC00u);
            ++i;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            SetError(error, L"文本包含孤立的 UTF-16 低代理项。");
            output.clear();
            return false;
        }

        if (cp <= 0x7F) {
            output.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
            output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
            output.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            output.push_back(static_cast<char>(0xF0u | (cp >> 18u)));
            output.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return true;
}

bool Utf8Decode(std::string_view bytes, std::wstring& output,
                std::wstring* error) {
    output.clear();
    output.reserve(bytes.size());
    std::size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char first = static_cast<unsigned char>(bytes[i]);
        std::uint32_t cp = 0;
        std::size_t length = 0;
        std::uint32_t minimum = 0;

        if (first < 0x80) {
            cp = first;
            length = 1;
            minimum = 0;
        } else if ((first & 0xE0) == 0xC0) {
            cp = first & 0x1F;
            length = 2;
            minimum = 0x80;
        } else if ((first & 0xF0) == 0xE0) {
            cp = first & 0x0F;
            length = 3;
            minimum = 0x800;
        } else if ((first & 0xF8) == 0xF0) {
            cp = first & 0x07;
            length = 4;
            minimum = 0x10000;
        } else {
            SetError(error, L"文本包含无效的 UTF-8 起始字节。");
            output.clear();
            return false;
        }

        if (i + length > bytes.size()) {
            SetError(error, L"UTF-8 文本被截断。");
            output.clear();
            return false;
        }
        for (std::size_t j = 1; j < length; ++j) {
            const unsigned char cont = static_cast<unsigned char>(bytes[i + j]);
            if ((cont & 0xC0) != 0x80) {
                SetError(error, L"文本包含无效的 UTF-8 后续字节。");
                output.clear();
                return false;
            }
            cp = (cp << 6u) | (cont & 0x3Fu);
        }
        if (length > 1 && cp < minimum) {
            SetError(error, L"UTF-8 文本包含过长编码。");
            output.clear();
            return false;
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            SetError(error, L"UTF-8 文本包含非法 Unicode 码点。");
            output.clear();
            return false;
        }

        if (cp <= 0xFFFF) {
            output.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000u;
            output.push_back(static_cast<wchar_t>(0xD800u + (cp >> 10u)));
            output.push_back(static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu)));
        }
        i += length;
    }
    return true;
}

std::string Utf8Encode(std::wstring_view text) {
    std::string output;
    Utf8Encode(text, output, nullptr);
    return output;
}

std::wstring Utf8Decode(std::string_view bytes) {
    std::wstring output;
    Utf8Decode(bytes, output, nullptr);
    return output;
}

}  // namespace mdge
