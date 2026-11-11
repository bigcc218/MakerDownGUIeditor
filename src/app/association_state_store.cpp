#include "association_state_store.h"

#include "association_diagnostics.h"

#include <windows.h>

#include <map>
#include <string>

namespace mdgeui {
namespace {

std::wstring StateDirectory() {
    return AssociationDataDirectory();
}

std::wstring TempPathFor(const std::wstring& path) {
    return path + L".tmp";
}

std::wstring AsciiToWide(const std::string& text) {
    std::wstring result;
    result.reserve(text.size());
    for (char ch : text) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    }
    return result;
}

std::string WideToAscii(const std::wstring& text) {
    std::string result;
    result.reserve(text.size());
    for (wchar_t ch : text) {
        result.push_back(static_cast<char>(ch & 0xff));
    }
    return result;
}

bool WriteTextFileAtomic(const std::wstring& path,
                         const std::map<std::string, std::string>& values) {
    const std::wstring temp = TempPathFor(path);
    std::string content;
    for (const auto& [key, value] : values) {
        content += key;
        content += '=';
        content += value;
        content += "\r\n";
    }
    HANDLE file = CreateFileW(
        temp.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool writeOk =
        !content.empty() &&
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()),
                  &written, nullptr) &&
        written == content.size();
    CloseHandle(file);
    if (!writeOk) {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

std::map<std::string, std::string> ReadTextFile(const std::wstring& path) {
    std::map<std::string, std::string> values;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return values;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return values;
    }
    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool readOk =
        ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read,
                 nullptr) &&
        read == content.size();
    CloseHandle(file);
    if (!readOk) {
        return values;
    }

    std::size_t position = 0;
    while (position < content.size()) {
        std::size_t end = content.find('\n', position);
        if (end == std::string::npos) {
            end = content.size();
        }
        std::string line = content.substr(position, end - position);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t equals = line.find('=');
        if (equals != std::string::npos) {
            values[line.substr(0, equals)] = line.substr(equals + 1);
        }
        position = end + 1;
    }
    return values;
}

std::uint64_t ParseUint64(const std::map<std::string, std::string>& values,
                          const char* key) {
    const auto found = values.find(key);
    if (found == values.end()) {
        return 0;
    }
    try {
        return std::stoull(found->second);
    } catch (...) {
        return 0;
    }
}

std::string ToString(std::uint64_t value) {
    return std::to_string(value);
}

bool ToBool(const std::map<std::string, std::string>& values, const char* key) {
    const auto found = values.find(key);
    return found != values.end() && found->second != "0";
}

}  // namespace

std::wstring AssociationStateFilePath() {
    const std::wstring directory = StateDirectory();
    return directory.empty() ? std::wstring()
                             : directory + L"\\association-state.ini";
}

std::wstring UserChoiceBackupFilePath() {
    const std::wstring directory = StateDirectory();
    return directory.empty() ? std::wstring()
                             : directory + L"\\association-userchoice.ini";
}

bool LoadAssociationFileStateAtPath(const std::wstring& path,
                                    AssociationFileState& state) {
    state = {};
    if (path.empty()) {
        return false;
    }
    const auto values = ReadTextFile(path);
    if (values.empty()) {
        return false;
    }
    state.valid = true;
    state.version2 = ToBool(values, "version2");
    state.projectReady = ToBool(values, "projectReady");
    state.markdownPromptDone = ToBool(values, "markdownPromptDone");
    state.markdownRequested = ToBool(values, "markdownRequested");
    state.lastError = static_cast<std::uint32_t>(
        ParseUint64(values, "lastError"));
    state.lastRecoveryPromptVersion = static_cast<std::uint32_t>(
        ParseUint64(values, "lastRecoveryPromptVersion"));
    state.lastRecoveryPromptTime = ParseUint64(values, "lastRecoveryPromptTime");
    return true;
}

bool SaveAssociationFileStateAtPath(const std::wstring& path,
                                    const AssociationFileState& state) {
    if (path.empty()) {
        return false;
    }
    std::map<std::string, std::string> values;
    values["version2"] = state.version2 ? "1" : "0";
    values["projectReady"] = state.projectReady ? "1" : "0";
    values["markdownPromptDone"] = state.markdownPromptDone ? "1" : "0";
    values["markdownRequested"] = state.markdownRequested ? "1" : "0";
    values["lastError"] = ToString(state.lastError);
    values["lastRecoveryPromptVersion"] =
        ToString(state.lastRecoveryPromptVersion);
    values["lastRecoveryPromptTime"] =
        ToString(state.lastRecoveryPromptTime);
    return WriteTextFileAtomic(path, values);
}

bool LoadAssociationFileState(AssociationFileState& state) {
    return LoadAssociationFileStateAtPath(AssociationStateFilePath(), state);
}

bool SaveAssociationFileState(const AssociationFileState& state) {
    return SaveAssociationFileStateAtPath(AssociationStateFilePath(), state);
}

bool LoadUserChoiceBackupFileAtPath(const std::wstring& path,
                                    UserChoiceBackup& backup) {
    backup = {};
    if (path.empty()) {
        return false;
    }
    const auto values = ReadTextFile(path);
    if (values.empty()) {
        return false;
    }
    const auto exists = values.find("exists");
    if (exists == values.end()) {
        return false;
    }
    backup.exists = exists->second != "0";
    const auto progId = values.find("progId");
    if (progId != values.end()) {
        backup.progId = AsciiToWide(progId->second);
    }
    const auto hash = values.find("hash");
    if (hash != values.end()) {
        backup.hash = AsciiToWide(hash->second);
    }
    return true;
}

bool SaveUserChoiceBackupFileAtPath(const std::wstring& path,
                                    const UserChoiceBackup& backup) {
    if (path.empty()) {
        return false;
    }
    std::map<std::string, std::string> values;
    values["exists"] = backup.exists ? "1" : "0";
    values["progId"] = WideToAscii(backup.progId);
    values["hash"] = WideToAscii(backup.hash);
    return WriteTextFileAtomic(path, values);
}

bool LoadUserChoiceBackupFile(UserChoiceBackup& backup) {
    return LoadUserChoiceBackupFileAtPath(UserChoiceBackupFilePath(), backup);
}

bool SaveUserChoiceBackupFile(const UserChoiceBackup& backup) {
    return SaveUserChoiceBackupFileAtPath(UserChoiceBackupFilePath(), backup);
}

bool ClearUserChoiceBackupFile() {
    const std::wstring path = UserChoiceBackupFilePath();
    if (path.empty()) {
        return false;
    }
    DeleteFileW(path.c_str());
    DeleteFileW(TempPathFor(path).c_str());
    return true;
}

}  // namespace mdgeui
