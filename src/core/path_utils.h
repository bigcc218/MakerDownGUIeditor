// MDGE Studio - path resolution and portable resource lookup
#pragma once

#include <string>

namespace mdge {

enum class AssetPathKind {
    Empty,
    Absolute,
    ProjectRelative,
    WorkingDirectoryRelative,
    ApplicationRelative,
    Missing,
};

struct ResolvedAssetPath {
    std::wstring storedPath;
    std::wstring resolvedPath;
    std::wstring originLabel;
    AssetPathKind kind = AssetPathKind::Empty;
    bool exists = false;
};

std::wstring TrimText(const std::wstring& text);
std::wstring CleanPathInput(const std::wstring& input);
bool IsAbsolutePath(const std::wstring& path);
bool IsFileExisting(const std::wstring& path);
bool IsDirectoryExisting(const std::wstring& path);
std::wstring GetAbsolutePath(const std::wstring& path);
std::wstring DirectoryName(const std::wstring& path);
std::wstring FileName(const std::wstring& path);
std::wstring FileStem(const std::wstring& path);
std::wstring FileExtensionLower(const std::wstring& path);
std::wstring CurrentDirectoryPath();
std::wstring ExecutableDirectoryPath();
std::wstring ProjectDirectoryFor(const std::wstring& projectFilePath);
std::wstring ToMarkdownPath(const std::wstring& path);
std::wstring SanitizeFileStem(const std::wstring& text);

// Supports both absolute paths and relative paths. Relative paths are searched
// next to the .mdge project first, then the working directory, then the folder
// containing the executable. The selected origin is reported in the result.
ResolvedAssetPath ResolveAssetPath(const std::wstring& storedPath,
                                   const std::wstring& projectFilePath);

// Converts targetPath to a path relative to baseDirectory. Returns false for
// different volumes or unresolved inputs.
bool TryMakeRelativePath(const std::wstring& baseDirectory,
                         const std::wstring& targetPath,
                         std::wstring& relativePath);

bool HasImageExtension(const std::wstring& path);

}  // namespace mdge
