// Testable file-association decisions and registry value construction.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mdgeui {

constexpr int kAppIconResourceId = 101;
constexpr int kMdgeFileIconResourceId = 102;
constexpr int kMdFileIconResourceId = 103;

constexpr int kAssociationExitSuccess = 0;
constexpr int kAssociationExitAccessDenied = 5;
constexpr int kAssociationExitSidMismatch = 87;

enum class RegistryValueType {
    String,
    Dword,
    None,
};

struct RegistryEntry {
    std::wstring subkey;
    std::wstring valueName;
    RegistryValueType type = RegistryValueType::String;
    std::wstring stringValue;
    std::uint32_t dwordValue = 0;
};

struct AssociationSettings {
    bool version2 = false;
    bool legacySetupDone = false;
    bool projectReady = false;
    bool markdownPromptDone = false;
    bool markdownRequested = false;
    std::uint32_t lastError = 0;
    std::uint32_t lastRecoveryPromptVersion = 0;
    std::uint64_t lastRecoveryPromptTime = 0;
};

struct AssociationObservation {
    bool projectAssociated = false;
    bool projectIsOurs = false;
    bool markdownAssociated = false;
    bool markdownIsOurs = false;
    bool projectUsesOpenWithFallback = false;
    bool markdownUsesOpenWithFallback = false;
};

struct AssociationDecision {
    bool attemptProject = false;
    bool promptMarkdown = false;
    bool retryMarkdown = false;
};

struct AssociationFileRequest {
    bool mdge = false;
    bool md = false;
};

struct UserChoiceBackup {
    bool exists = false;
    std::wstring progId;
    std::wstring hash;
};

// Applies the conservative migration requested for the old single marker:
// old users are considered to have been asked about .md already.
void ApplyLegacyMigration(AssociationSettings& settings);

AssociationDecision DecideAssociationActions(const AssociationSettings& settings,
                                             const AssociationObservation& observed);

bool IsOpenWithFallbackExecutable(const std::wstring& executablePath);
bool PathsEqual(const std::wstring& left, const std::wstring& right);
bool SidsEqual(const std::wstring& left, const std::wstring& right);

std::wstring DefaultIconRegistrationValue(const std::wstring& executablePath,
                                          int iconResourceId);
std::wstring OpenCommandRegistrationValue(const std::wstring& executablePath);

std::vector<RegistryEntry> BuildRegistrationEntries(
    const std::wstring& executablePath, bool setMdgeDefault, bool setMdDefault);

std::wstring BuildRecoveryRegistrationArguments(
    const AssociationFileRequest& request, const std::wstring& expectedUserSid);
bool ParseRecoveryRegistrationArguments(
    const std::vector<std::wstring>& arguments, AssociationFileRequest& request,
    std::wstring& expectedUserSid);

bool ShouldPromptForRecovery(std::uint32_t lastPromptVersion,
                             std::uint64_t lastPromptTime, std::uint64_t now,
                             std::uint32_t currentPromptVersion,
                             std::uint64_t throttleSeconds, bool manual);

bool ShouldRestoreUserChoiceOnFailure(const UserChoiceBackup& backup);

}  // namespace mdgeui
