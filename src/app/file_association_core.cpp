#include "file_association_core.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace mdgeui {
namespace {

constexpr wchar_t kClassesRoot[] = L"Software\\Classes";
constexpr wchar_t kSettingsRoot[] = L"Software\\MDGE Studio";
constexpr wchar_t kProjectProgId[] = L"MDGEStudio.Project";
constexpr wchar_t kMarkdownProgId[] = L"MDGEStudio.Markdown";
constexpr wchar_t kProjectTypeName[] = L"MDGE Studio 项目";
constexpr wchar_t kMarkdownTypeName[] = L"Markdown 文档";
constexpr wchar_t kAppRegistrationName[] = L"MDGE Studio";

std::wstring FileNamePart(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool EqualCaseInsensitive(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(static_cast<wint_t>(left[i])) !=
            std::towlower(static_cast<wint_t>(right[i]))) {
            return false;
        }
    }
    return true;
}

void AddStringEntry(std::vector<RegistryEntry>& entries,
                    const std::wstring& subkey, const std::wstring& valueName,
                    const std::wstring& value) {
    entries.push_back(RegistryEntry{subkey, valueName, RegistryValueType::String,
                                    value, 0});
}

void AddAppCapabilities(std::vector<RegistryEntry>& entries,
                        const std::wstring& executablePath) {
    const std::wstring appKey =
        std::wstring(kClassesRoot) + L"\\Applications\\MDGEStudio.exe";
    AddStringEntry(entries, appKey, L"", kAppRegistrationName);
    AddStringEntry(entries, appKey, L"FriendlyAppName", kAppRegistrationName);
    AddStringEntry(entries, appKey, L"DefaultIcon",
                   DefaultIconRegistrationValue(executablePath,
                                                kAppIconResourceId));
    AddStringEntry(entries, appKey + L"\\shell\\open\\command", L"",
                   OpenCommandRegistrationValue(executablePath));
    entries.push_back(RegistryEntry{appKey + L"\\SupportedTypes", L".mdge",
                                    RegistryValueType::None, L"", 0});
    entries.push_back(RegistryEntry{appKey + L"\\SupportedTypes", L".md",
                                    RegistryValueType::None, L"", 0});

    const std::wstring capabilities = std::wstring(kSettingsRoot) + L"\\Capabilities";
    AddStringEntry(entries, capabilities, L"", kAppRegistrationName);
    AddStringEntry(entries, capabilities, L"ApplicationName",
                   kAppRegistrationName);
    AddStringEntry(entries, capabilities, L"ApplicationDescription",
                   L"Visual Markdown editor");
    AddStringEntry(entries, capabilities, L"ApplicationIcon",
                   DefaultIconRegistrationValue(executablePath,
                                                kAppIconResourceId));
    AddStringEntry(entries, capabilities + L"\\FileAssociations", L".mdge",
                   kProjectProgId);
    AddStringEntry(entries, capabilities + L"\\FileAssociations", L".md",
                   kMarkdownProgId);

    AddStringEntry(entries, kSettingsRoot, L"RegisteredApplicationName",
                   kAppRegistrationName);
    AddStringEntry(entries, kSettingsRoot, L"CapabilitiesPath", capabilities);
    AddStringEntry(entries, L"Software\\RegisteredApplications",
                   kAppRegistrationName, capabilities);
}

void AddProgIdEntries(std::vector<RegistryEntry>& entries,
                      const std::wstring& progId, const std::wstring& typeName,
                      const std::wstring& executablePath, int iconResourceId) {
    const std::wstring root = std::wstring(kClassesRoot) + L"\\" + progId;
    AddStringEntry(entries, root, L"", typeName);
    AddStringEntry(entries, root, L"FriendlyTypeName", typeName);
    AddStringEntry(entries, root + L"\\DefaultIcon", L"",
                   DefaultIconRegistrationValue(executablePath, iconResourceId));
    AddStringEntry(entries, root + L"\\shell\\open\\command", L"",
                   OpenCommandRegistrationValue(executablePath));
}

}  // namespace

void ApplyLegacyMigration(AssociationSettings& settings) {
    if (!settings.version2 && settings.legacySetupDone) {
        // Old versions may have burned the one-time marker even when the
        // association write failed. Do not repeat the .md question for those
        // users; expose a manual repair entry instead.
        settings.markdownPromptDone = true;
        settings.version2 = true;
    }
}

AssociationDecision DecideAssociationActions(
    const AssociationSettings& settings,
    const AssociationObservation& observed) {
    AssociationDecision decision;
    if (!observed.projectIsOurs && !observed.projectAssociated) {
        decision.attemptProject = true;
    }
    const bool projectAvailable =
        observed.projectIsOurs || observed.projectAssociated;
    if (projectAvailable && !observed.markdownIsOurs) {
        if (settings.markdownRequested) {
            decision.retryMarkdown = true;
        } else if (!settings.markdownPromptDone) {
            decision.promptMarkdown = true;
        }
    }
    return decision;
}

bool IsOpenWithFallbackExecutable(const std::wstring& executablePath) {
    const std::wstring fileName = FileNamePart(executablePath);
    return EqualCaseInsensitive(fileName, L"OpenWith.exe") ||
           EqualCaseInsensitive(fileName, L"OpenWithHost.exe");
}

bool PathsEqual(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) {
        return false;
    }
    return EqualCaseInsensitive(left, right);
}

bool SidsEqual(const std::wstring& left, const std::wstring& right) {
    return !left.empty() && !right.empty() &&
           EqualCaseInsensitive(left, right);
}

std::wstring DefaultIconRegistrationValue(const std::wstring& executablePath,
                                          int iconResourceId) {
    return L"\"" + executablePath + L"\"," +
           std::to_wstring(-iconResourceId);
}

std::wstring OpenCommandRegistrationValue(const std::wstring& executablePath) {
    return L"\"" + executablePath + L"\" \"%1\"";
}

std::vector<RegistryEntry> BuildRegistrationEntries(
    const std::wstring& executablePath, bool setMdgeDefault,
    bool setMdDefault) {
    std::vector<RegistryEntry> entries;
    const std::wstring classes = kClassesRoot;

    if (setMdgeDefault) {
        AddStringEntry(entries, classes + L"\\.mdge", L"", kProjectProgId);
    }
    entries.push_back(RegistryEntry{classes + L"\\.mdge\\OpenWithProgids",
                                    kProjectProgId, RegistryValueType::None,
                                    L"", 0});
    AddProgIdEntries(entries, kProjectProgId, kProjectTypeName, executablePath,
                     kMdgeFileIconResourceId);

    if (setMdDefault) {
        AddStringEntry(entries, classes + L"\\.md", L"", kMarkdownProgId);
    }
    entries.push_back(RegistryEntry{classes + L"\\.md\\OpenWithProgids",
                                    kMarkdownProgId, RegistryValueType::None,
                                    L"", 0});
    AddProgIdEntries(entries, kMarkdownProgId, kMarkdownTypeName, executablePath,
                     kMdFileIconResourceId);

    AddAppCapabilities(entries, executablePath);
    return entries;
}

std::wstring BuildRecoveryRegistrationArguments(
    const AssociationFileRequest& request, const std::wstring& expectedUserSid) {
    std::wstring mode;
    if (request.mdge && request.md) {
        mode = L"both";
    } else if (request.md) {
        mode = L"md";
    } else {
        mode = L"mdge";
    }
    return L"--register-file-associations=" + mode +
           L" --expected-user-sid=" + expectedUserSid;
}

bool ParseRecoveryRegistrationArguments(
    const std::vector<std::wstring>& arguments, AssociationFileRequest& request,
    std::wstring& expectedUserSid) {
    constexpr wchar_t kRegistrationPrefix[] = L"--register-file-associations=";
    constexpr wchar_t kSidPrefix[] = L"--expected-user-sid=";
    request = {};
    expectedUserSid.clear();
    bool sawMode = false;
    for (const std::wstring& argument : arguments) {
        if (argument.rfind(kRegistrationPrefix, 0) == 0) {
            const std::wstring mode = argument.substr(
                static_cast<std::size_t>(std::wcslen(kRegistrationPrefix)));
            if (mode == L"both") {
                request.mdge = true;
                request.md = true;
            } else if (mode == L"mdge") {
                request.mdge = true;
            } else if (mode == L"md") {
                request.md = true;
            } else {
                return false;
            }
            sawMode = true;
        } else if (argument.rfind(kSidPrefix, 0) == 0) {
            expectedUserSid = argument.substr(
                static_cast<std::size_t>(std::wcslen(kSidPrefix)));
        }
    }
    return sawMode && (request.mdge || request.md) && !expectedUserSid.empty();
}

bool ShouldPromptForRecovery(std::uint32_t lastPromptVersion,
                             std::uint64_t lastPromptTime, std::uint64_t now,
                             std::uint32_t currentPromptVersion,
                             std::uint64_t throttleSeconds, bool manual) {
    if (manual) {
        return true;
    }
    if (lastPromptVersion != currentPromptVersion) {
        return true;
    }
    if (lastPromptTime == 0) {
        return true;
    }
    if (now <= lastPromptTime) {
        return true;
    }
    return now - lastPromptTime >= throttleSeconds;
}

bool ShouldRestoreUserChoiceOnFailure(const UserChoiceBackup& backup) {
    return backup.exists && (!backup.progId.empty() || !backup.hash.empty());
}

}  // namespace mdgeui
