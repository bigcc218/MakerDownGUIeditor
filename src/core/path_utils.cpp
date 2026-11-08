#include "path_utils.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <vector>

namespace mdge {
namespace {

std::wstring GetFullPath(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }
    DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0) {
        return path;
    }
    std::wstring buffer(required, L'\0');
    const DWORD written =
        GetFullPathNameW(path.c_str(), required, buffer.data(), nullptr);
    if (written == 0 || written >= required) {
        return path;
    }
    buffer.resize(written);
    return buffer;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring LowerExtensionOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos ||
        (slash != std::wstring::npos && dot < slash)) {
        return {};
    }
    std::wstring extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension;
}

}  // namespace

std::wstring TrimText(const std::wstring& text) {
    const auto isSpace = [](wchar_t ch) {
        return iswspace(static_cast<wint_t>(ch)) != 0;
    };
    auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    auto end = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    if (begin >= end) {
        return {};
    }
    return std::wstring(begin, end);
}

std::wstring CleanPathInput(const std::wstring& input) {
    std::wstring value = TrimText(input);
    if (value.size() >= 2 &&
        ((value.front() == L'"' && value.back() == L'"') ||
         (value.front() == L'\'' && value.back() == L'\''))) {
        value = value.substr(1, value.size() - 2);
        value = TrimText(value);
    }
    std::replace(value.begin(), value.end(), L'/', L'\\');
    return value;
}

bool IsAbsolutePath(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        return true;  // UNC
    }
    if (path.size() >= 2 && path[1] == L':') {
        return true;  // drive path; drive-relative is normalized by Win32
    }
    if (path[0] == L'\\') {
        return true;
    }
    return false;
}

bool IsFileExisting(const std::wstring& path) {
    return IsFile(path);
}

bool IsDirectoryExisting(const std::wstring& path) {
    return IsDirectory(path);
}

std::wstring GetAbsolutePath(const std::wstring& path) {
    return GetFullPath(path);
}

std::wstring DirectoryName(const std::wstring& path) {
    const std::wstring full = GetFullPath(path);
    if (full.empty()) {
        return {};
    }
    std::size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    if (slash == 0) {
        return full.substr(0, 1);
    }
    if (slash == 2 && full.size() >= 3 && full[1] == L':') {
        return full.substr(0, 3);
    }
    if (slash == 1 && full.size() >= 2 && full[0] == L'\\') {
        return full.substr(0, 2);
    }
    return full.substr(0, slash);
}

std::wstring FileName(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring FileStem(const std::wstring& path) {
    std::wstring name = FileName(path);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) {
        return name;
    }
    name.resize(dot);
    return name;
}

std::wstring FileExtensionLower(const std::wstring& path) {
    return LowerExtensionOf(path);
}

std::wstring CurrentDirectoryPath() {
    DWORD required = GetCurrentDirectoryW(0, nullptr);
    if (required == 0) {
        return {};
    }
    std::wstring buffer(required, L'\0');
    const DWORD written = GetCurrentDirectoryW(required, buffer.data());
    if (written == 0 || written >= required) {
        return {};
    }
    buffer.resize(written);
    return buffer;
}

std::wstring ExecutableDirectoryPath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size() - 1) {
            buffer.resize(written);
            break;
        }
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 32768) {
            return {};
        }
    }
    return DirectoryName(buffer);
}

std::wstring ProjectDirectoryFor(const std::wstring& projectFilePath) {
    if (!projectFilePath.empty()) {
        const std::wstring directory = DirectoryName(projectFilePath);
        if (!directory.empty()) {
            return directory;
        }
    }
    return CurrentDirectoryPath();
}

std::wstring ToMarkdownPath(const std::wstring& path) {
    std::wstring result = path;
    std::replace(result.begin(), result.end(), L'\\', L'/');
    return result;
}

std::wstring SanitizeFileStem(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' || ch == L'/' ||
            ch == L'\\' || ch == L'|' || ch == L'?' || ch == L'*' ||
            static_cast<std::uint32_t>(ch) < 32) {
            result.push_back(L'_');
        } else {
            result.push_back(ch);
        }
    }
    result = TrimText(result);
    while (!result.empty() && (result.back() == L'.' || result.back() == L' ')) {
        result.pop_back();
    }
    if (result.empty()) {
        result = L"Untitled";
    }
    if (result.size() > 80) {
        result.resize(80);
    }
    return result;
}

ResolvedAssetPath ResolveAssetPath(const std::wstring& storedPath,
                                   const std::wstring& projectFilePath) {
    ResolvedAssetPath result;
    result.storedPath = CleanPathInput(storedPath);
    if (result.storedPath.empty()) {
        result.kind = AssetPathKind::Empty;
        return result;
    }

    if (IsAbsolutePath(result.storedPath)) {
        result.resolvedPath = GetFullPath(result.storedPath);
        result.kind = AssetPathKind::Absolute;
        result.originLabel = L"绝对路径";
        result.exists = IsFile(result.resolvedPath);
        if (!result.exists) {
            result.kind = AssetPathKind::Missing;
            result.originLabel = L"绝对路径（文件不存在）";
        }
        return result;
    }

    struct Candidate {
        std::wstring base;
        std::wstring label;
        AssetPathKind kind;
    };

    std::vector<Candidate> candidates;
    const std::wstring projectDirectory = ProjectDirectoryFor(projectFilePath);
    if (!projectFilePath.empty()) {
        candidates.push_back({projectDirectory, L"相对路径（项目目录）",
                              AssetPathKind::ProjectRelative});
    }
    const std::wstring cwd = CurrentDirectoryPath();
    candidates.push_back({cwd, L"相对路径（当前目录）",
                          AssetPathKind::WorkingDirectoryRelative});
    const std::wstring appDirectory = ExecutableDirectoryPath();
    candidates.push_back({appDirectory, L"相对路径（程序目录）",
                          AssetPathKind::ApplicationRelative});

    std::vector<std::wstring> seen;
    std::wstring firstResolved;
    for (const Candidate& candidate : candidates) {
        if (candidate.base.empty()) {
            continue;
        }
        const std::wstring combined = GetFullPath(candidate.base + L"\\" + result.storedPath);
        if (combined.empty()) {
            continue;
        }
        bool duplicate = false;
        for (const std::wstring& used : seen) {
            if (_wcsicmp(used.c_str(), combined.c_str()) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        seen.push_back(combined);
        if (firstResolved.empty()) {
            firstResolved = combined;
        }
        if (IsFile(combined)) {
            result.resolvedPath = combined;
            result.exists = true;
            result.kind = candidate.kind;
            result.originLabel = candidate.label;
            return result;
        }
    }

    result.kind = AssetPathKind::Missing;
    result.resolvedPath = firstResolved;
    result.originLabel = L"未找到资源";
    result.exists = false;
    return result;
}

bool TryMakeRelativePath(const std::wstring& baseDirectory,
                         const std::wstring& targetPath,
                         std::wstring& relativePath) {
    relativePath.clear();
    if (baseDirectory.empty() || targetPath.empty()) {
        return false;
    }
    std::wstring base = GetFullPath(baseDirectory);
    std::wstring target = GetFullPath(targetPath);
    if (base.empty() || target.empty()) {
        return false;
    }
    if (base.size() >= 2 && base[1] == L':') {
        if (target.size() < 2 || _wcsicmp(base.substr(0, 2).c_str(), target.substr(0, 2).c_str()) != 0) {
            return false;
        }
    }

    wchar_t buffer[32768]{};
    const DWORD baseAttributes = IsDirectory(base) ? FILE_ATTRIBUTE_DIRECTORY
                                                   : FILE_ATTRIBUTE_NORMAL;
    const DWORD targetAttributes = IsDirectory(target) ? FILE_ATTRIBUTE_DIRECTORY
                                                       : FILE_ATTRIBUTE_NORMAL;
    if (!PathRelativePathToW(buffer, base.c_str(), baseAttributes, target.c_str(),
                             targetAttributes)) {
        return false;
    }
    std::wstring value = buffer;
    if (value.rfind(L".\\", 0) == 0) {
        value = value.substr(2);
    }
    if (value.empty() || value == L".") {
        return false;
    }
    std::replace(value.begin(), value.end(), L'/', L'\\');
    relativePath = value;
    return true;
}

bool HasImageExtension(const std::wstring& path) {
    const std::wstring extension = FileExtensionLower(path);
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
           extension == L".bmp" || extension == L".gif" || extension == L".webp" ||
           extension == L".ico";
}

}  // namespace mdge
