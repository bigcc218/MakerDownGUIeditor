#include "file_utils.h"

#include <windows.h>

#include <algorithm>

namespace mdge {

std::wstring Win32ErrorText(unsigned long errorCode) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    const DWORD length = FormatMessageW(
        flags, nullptr, errorCode, language,
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result;
    if (length != 0 && buffer != nullptr) {
        result.assign(buffer, length);
        while (!result.empty() &&
               (result.back() == L'\r' || result.back() == L'\n' ||
                result.back() == L' ' || result.back() == L'.')) {
            result.pop_back();
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    if (result.empty()) {
        wchar_t fallback[64]{};
        swprintf(fallback, 64, L"Windows 错误 %lu", errorCode);
        result = fallback;
    }
    return result;
}

std::wstring LastWin32ErrorText() {
    return Win32ErrorText(GetLastError());
}

bool ReadFileBytes(const std::wstring& path, std::vector<std::uint8_t>& bytes,
                   std::wstring& error, std::uint64_t maxSize) {
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"无法打开文件：" + path + L"\n" + LastWin32ErrorText();
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        error = L"无法读取文件大小：" + LastWin32ErrorText();
        CloseHandle(file);
        return false;
    }
    if (size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > maxSize) {
        error = L"文件过大，无法载入。";
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(bytes.size() - offset, 1u << 24));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr)) {
            error = L"读取文件失败：" + LastWin32ErrorText();
            bytes.clear();
            CloseHandle(file);
            return false;
        }
        if (read == 0) {
            break;
        }
        offset += read;
    }
    CloseHandle(file);
    if (offset != bytes.size()) {
        bytes.resize(offset);
    }
    return true;
}

bool WriteFileBytesAtomic(const std::wstring& path,
                          const std::vector<std::uint8_t>& bytes,
                          std::wstring& error) {
    if (path.empty()) {
        error = L"文件路径为空。";
        return false;
    }

    // Keep the temporary file beside the destination so replacing it is an
    // atomic same-volume rename and does not depend on the system temp folder.
    std::wstring targetDirectory;
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        targetDirectory = path.substr(0, slash);
        if (targetDirectory.empty()) {
            targetDirectory = L"\\";
        }
    } else {
        targetDirectory = L".";
    }

    wchar_t tempPath[MAX_PATH + 1]{};
    if (GetTempFileNameW(targetDirectory.c_str(), L"mge", 0, tempPath) == 0) {
        error = L"无法创建临时文件：" + LastWin32ErrorText();
        return false;
    }

    HANDLE file = CreateFileW(tempPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"无法创建临时文件：" + LastWin32ErrorText();
        DeleteFileW(tempPath);
        return false;
    }

    bool ok = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(bytes.size() - offset, 1u << 24));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            error = L"写入文件失败：" + LastWin32ErrorText();
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok && !FlushFileBuffers(file)) {
        error = L"刷新文件失败：" + LastWin32ErrorText();
        ok = false;
    }
    CloseHandle(file);

    if (ok) {
        if (!MoveFileExW(tempPath, path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = L"无法写入目标文件：" + path + L"\n" + LastWin32ErrorText();
            ok = false;
        }
    }
    if (!ok) {
        DeleteFileW(tempPath);
    }
    return ok;
}

bool WriteUtf8TextFile(const std::wstring& path, const std::string& utf8,
                       std::wstring& error) {
    std::vector<std::uint8_t> bytes(utf8.begin(), utf8.end());
    return WriteFileBytesAtomic(path, bytes, error);
}

bool EnsureDirectoryExists(const std::wstring& directory, std::wstring& error) {
    if (directory.empty()) {
        error = L"目录为空。";
        return false;
    }
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return true;
    }

    std::wstring prefix;
    std::size_t index = 0;
    if (directory.size() >= 2 && directory[1] == L':') {
        prefix = directory.substr(0, 2);
        index = 2;
        if (index < directory.size() && (directory[index] == L'\\' || directory[index] == L'/')) {
            prefix.push_back(L'\\');
            ++index;
        }
    } else if (directory.size() >= 2 && directory[0] == L'\\' &&
               directory[1] == L'\\') {
        prefix = L"\\\\";
        index = 2;
    }

    while (index <= directory.size()) {
        const std::size_t next = directory.find_first_of(L"\\/", index);
        const std::size_t end = next == std::wstring::npos ? directory.size() : next;
        std::wstring part = directory.substr(index, end - index);
        if (!part.empty()) {
            if (!prefix.empty() && prefix.back() != L'\\') {
                prefix.push_back(L'\\');
            }
            prefix += part;
            const DWORD attr = GetFileAttributesW(prefix.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(prefix.c_str(), nullptr) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                    error = L"无法创建目录：" + prefix + L"\n" + LastWin32ErrorText();
                    return false;
                }
            } else if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                error = L"路径已存在且不是目录：" + prefix;
                return false;
            }
        }
        if (next == std::wstring::npos) {
            break;
        }
        index = next + 1;
    }
    return true;
}

}  // namespace mdge
