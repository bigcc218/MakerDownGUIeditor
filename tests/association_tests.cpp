#include "app/association_diagnostics.h"
#include "app/association_state_store.h"
#include "app/file_association_core.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", message);
    }
}

const mdgeui::RegistryEntry* FindEntry(
    const std::vector<mdgeui::RegistryEntry>& entries,
    const std::wstring& subkey, const std::wstring& valueName) {
    for (const mdgeui::RegistryEntry& entry : entries) {
        if (entry.subkey == subkey && entry.valueName == valueName) {
            return &entry;
        }
    }
    return nullptr;
}

void TestLegacyMigration() {
    mdgeui::AssociationSettings settings;
    settings.legacySetupDone = true;
    mdgeui::ApplyLegacyMigration(settings);
    Check(settings.version2, "legacy migration upgrades the setup version");
    Check(settings.markdownPromptDone,
          "legacy migration suppresses the automatic .md prompt");

    mdgeui::AssociationObservation observed;
    observed.projectIsOurs = true;
    observed.markdownAssociated = true;
    const mdgeui::AssociationDecision decision =
        mdgeui::DecideAssociationActions(settings, observed);
    Check(!decision.promptMarkdown,
          "legacy user is not asked about .md again");
}

void TestDecisionFlow() {
    mdgeui::AssociationSettings settings;
    mdgeui::AssociationObservation observed;
    mdgeui::AssociationDecision decision =
        mdgeui::DecideAssociationActions(settings, observed);
    Check(decision.attemptProject, "clean state attempts .mdge registration");
    Check(!decision.promptMarkdown,
          "clean state waits for .mdge before asking about .md");

    observed.projectIsOurs = true;
    observed.markdownAssociated = true;
    decision = mdgeui::DecideAssociationActions(settings, observed);
    Check(!decision.attemptProject, "working .mdge association is kept");
    Check(decision.promptMarkdown, "other .md handler triggers the prompt");

    settings.markdownPromptDone = true;
    decision = mdgeui::DecideAssociationActions(settings, observed);
    Check(!decision.promptMarkdown,
          "answered prompt is not shown again");

    settings = {};
    settings.markdownRequested = true;
    observed.markdownAssociated = true;
    decision = mdgeui::DecideAssociationActions(settings, observed);
    Check(decision.retryMarkdown,
          "accepted .md request is retried without a prompt");
    Check(!decision.promptMarkdown,
          "retry does not show the prompt again");

    settings = {};
    observed = {};
    observed.projectAssociated = true;
    observed.markdownAssociated = true;
    decision = mdgeui::DecideAssociationActions(settings, observed);
    Check(!decision.attemptProject,
          "another program's .mdge association is not overwritten");
    Check(decision.promptMarkdown,
          ".md can still be offered when .mdge belongs to another app");

    observed.markdownIsOurs = true;
    decision = mdgeui::DecideAssociationActions(settings, observed);
    Check(!decision.promptMarkdown,
          "existing self-owned .md association is not prompted");
}

void TestOpenWithAndPaths() {
    Check(mdgeui::IsOpenWithFallbackExecutable(
              L"C:\\Windows\\System32\\OpenWith.exe"),
          "OpenWith.exe is not a real association");
    Check(mdgeui::IsOpenWithFallbackExecutable(
              L"C:\\Windows\\SysWOW64\\OpenWithHost.exe"),
          "OpenWithHost.exe is not a real association");
    Check(!mdgeui::IsOpenWithFallbackExecutable(
              L"C:\\Apps\\MDGEStudio.exe"),
          "the app is not an OpenWith fallback");
    Check(mdgeui::PathsEqual(L"C:\\Apps\\MDGEStudio.exe",
                             L"c:\\apps\\mdgestudio.exe"),
          "path comparison is case-insensitive");
    Check(!mdgeui::PathsEqual(L"C:\\Apps\\MDGEStudio.exe",
                              L"C:\\Apps\\Other.exe"),
          "different paths do not compare equal");
}

void TestSidHandling() {
    Check(mdgeui::SidsEqual(L"S-1-5-21-100-200-300-1001",
                            L"s-1-5-21-100-200-300-1001"),
          "SID comparison is case-insensitive");
    Check(!mdgeui::SidsEqual(L"S-1-5-21-100-200-300-1001",
                             L"S-1-5-21-100-200-300-1002"),
          "different SIDs do not match");
    Check(!mdgeui::SidsEqual(L"", L"S-1-5-21-100-200-300-1001"),
          "empty SID cannot match");
}

void TestRegistrationValues() {
    const std::wstring exe = L"C:\\Apps\\MDGEStudio.exe";
    Check(mdgeui::DefaultIconRegistrationValue(
              exe, mdgeui::kMdgeFileIconResourceId) ==
              L"\"C:\\Apps\\MDGEStudio.exe\",-102",
          ".mdge DefaultIcon uses resource -102");
    Check(mdgeui::DefaultIconRegistrationValue(
              exe, mdgeui::kMdFileIconResourceId) ==
              L"\"C:\\Apps\\MDGEStudio.exe\",-103",
          ".md DefaultIcon uses resource -103");
    Check(mdgeui::DefaultIconRegistrationValue(
              exe, mdgeui::kAppIconResourceId) ==
              L"\"C:\\Apps\\MDGEStudio.exe\",-101",
          "application icon uses resource -101");
    Check(mdgeui::kAppIconResourceId != mdgeui::kMdgeFileIconResourceId &&
              mdgeui::kAppIconResourceId != mdgeui::kMdFileIconResourceId &&
              mdgeui::kMdgeFileIconResourceId != mdgeui::kMdFileIconResourceId,
          "all three icon resource IDs are distinct");
    Check(mdgeui::OpenCommandRegistrationValue(exe) ==
              L"\"C:\\Apps\\MDGEStudio.exe\" \"%1\"",
          "open command is correctly quoted");

    const std::vector<mdgeui::RegistryEntry> entries =
        mdgeui::BuildRegistrationEntries(exe, true, true);
    const std::wstring classes = L"Software\\Classes";
    const auto* mdgeDefault =
        FindEntry(entries, classes + L"\\.mdge", L"");
    const auto* mdDefault = FindEntry(entries, classes + L"\\.md", L"");
    Check(mdgeDefault && mdgeDefault->stringValue == L"MDGEStudio.Project",
          ".mdge extension points at its ProgID");
    Check(mdDefault && mdDefault->stringValue == L"MDGEStudio.Markdown",
          ".md extension points at its ProgID");
    const auto* mdgeIcon =
        FindEntry(entries, classes + L"\\MDGEStudio.Project\\DefaultIcon",
                  L"");
    const auto* mdIcon =
        FindEntry(entries, classes + L"\\MDGEStudio.Markdown\\DefaultIcon",
                  L"");
    Check(mdgeIcon && mdgeIcon->stringValue ==
                         L"\"C:\\Apps\\MDGEStudio.exe\",-102",
          "project ProgID has the .mdge icon");
    Check(mdIcon && mdIcon->stringValue ==
                        L"\"C:\\Apps\\MDGEStudio.exe\",-103",
          "markdown ProgID has the .md icon");
    Check(FindEntry(entries, classes + L"\\.mdge\\OpenWithProgids",
                    L"MDGEStudio.Project") != nullptr,
          ".mdge OpenWithProgids is registered");
    Check(FindEntry(entries, classes + L"\\.md\\OpenWithProgids",
                    L"MDGEStudio.Markdown") != nullptr,
          ".md OpenWithProgids is registered");

    const auto* registeredApp = FindEntry(
        entries, L"Software\\RegisteredApplications", L"MDGE Studio");
    Check(registeredApp != nullptr,
          "MDGE Studio is registered for Default Apps");
    Check(registeredApp &&
              registeredApp->stringValue ==
                  L"Software\\MDGE Studio\\Capabilities",
          "RegisteredApplications points at the matching Capabilities key");
    Check(FindEntry(entries, L"Software\\MDGE Studio\\Capabilities",
                    L"ApplicationName") != nullptr,
          "application capabilities are present");
    Check(FindEntry(entries, L"Software\\MDGE Studio\\Capabilities\\FileAssociations",
                    L".mdge") != nullptr,
          ".mdge capability is present");
    Check(FindEntry(entries, L"Software\\MDGE Studio\\Capabilities\\FileAssociations",
                    L".md") != nullptr,
          ".md capability is present");
    Check(FindEntry(entries,
                    L"Software\\Classes\\Applications\\MDGEStudio.exe",
                    L"FriendlyAppName") != nullptr,
          "application FriendlyAppName is registered");
    Check(FindEntry(entries,
                    L"Software\\Classes\\Applications\\MDGEStudio.exe\\SupportedTypes",
                    L".mdge") != nullptr &&
              FindEntry(entries,
                        L"Software\\Classes\\Applications\\MDGEStudio.exe\\SupportedTypes",
                        L".md") != nullptr,
          "application SupportedTypes contains both extensions");

    const std::vector<mdgeui::RegistryEntry> metadataOnly =
        mdgeui::BuildRegistrationEntries(exe, false, false);
    Check(FindEntry(metadataOnly, classes + L"\\.mdge", L"") == nullptr,
          "metadata-only registration does not overwrite .mdge default");
    Check(FindEntry(metadataOnly, classes + L"\\.md", L"") == nullptr,
          "metadata-only registration does not overwrite .md default");
}

void TestHelperArguments() {
    mdgeui::AssociationFileRequest request;
    std::wstring sid;
    Check(mdgeui::ParseRecoveryRegistrationArguments(
              {L"--register-file-associations=both",
               L"--expected-user-sid=S-1-5-21-100-200-300-1001"},
              request, sid),
          "hidden mode parses both extensions");
    Check(request.mdge && request.md, "both request sets both extensions");
    Check(sid == L"S-1-5-21-100-200-300-1001", "hidden mode parses SID");

    Check(!mdgeui::ParseRecoveryRegistrationArguments(
              {L"--register-file-associations=mdge"}, request, sid),
          "hidden mode requires the expected SID");
    Check(!mdgeui::ParseRecoveryRegistrationArguments(
              {L"--register-file-associations=invalid",
               L"--expected-user-sid=S-1-5-21-100-200-300-1001"},
              request, sid),
          "hidden mode rejects an invalid extension mode");

    const std::wstring built = mdgeui::BuildRecoveryRegistrationArguments(
        mdgeui::AssociationFileRequest{true, true},
        L"S-1-5-21-100-200-300-1001");
    Check(built.find(L"--register-file-associations=both") != std::wstring::npos &&
              built.find(L"--expected-user-sid=S-1-5-21-100-200-300-1001") !=
                  std::wstring::npos,
          "hidden mode arguments round-trip");
}

void TestRecoveryThrottle() {
    Check(mdgeui::ShouldPromptForRecovery(2, 1000, 2000, 2, 500, true),
          "manual repair always shows recovery");
    Check(mdgeui::ShouldPromptForRecovery(0, 0, 1000, 2, 86400, false),
          "first automatic failure shows recovery");
    Check(!mdgeui::ShouldPromptForRecovery(2, 1000, 2000, 2, 86400, false),
          "automatic recovery is throttled");
    Check(mdgeui::ShouldPromptForRecovery(1, 1000, 2000, 2, 86400, false),
          "a new recovery prompt version bypasses throttling");
    Check(mdgeui::ShouldPromptForRecovery(2, 1000, 1000 + 86400, 2, 86400,
                                          false),
          "recovery is available again after the throttle interval");
}

void TestUserChoiceBackup() {
    mdgeui::UserChoiceBackup backup;
    Check(!mdgeui::ShouldRestoreUserChoiceOnFailure(backup),
          "no backup means no UserChoice restore");
    backup.exists = true;
    backup.progId = L"VSCode.md";
    Check(mdgeui::ShouldRestoreUserChoiceOnFailure(backup),
          "existing UserChoice backup can be restored");
    backup.progId.clear();
    Check(!mdgeui::ShouldRestoreUserChoiceOnFailure(backup),
          "empty backup data is not treated as restorable");
}

void TestAssociationFileStore() {
    std::wstring baseDirectory = mdgeui::AssociationDataDirectory();
    // 数据目录可能因为沙箱、组策略或受限账户不可写；这是环境限制，
    // 不是状态存储的缺陷，退回 %TEMP% 继续验证读写往返。
    if (baseDirectory.empty() ||
        GetFileAttributesW(baseDirectory.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t tempPath[MAX_PATH]{};
        const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
        baseDirectory = (tempLength > 0 && tempLength < MAX_PATH)
                            ? std::wstring(tempPath)
                            : std::wstring();
    }
    if (baseDirectory.empty()) {
        std::printf("SKIP: no writable directory for the association state "
                    "test.\n");
        return;
    }
    while (!baseDirectory.empty() && baseDirectory.back() == L'\\') {
        baseDirectory.pop_back();
    }
    const std::wstring directory =
        baseDirectory + L"\\StateStoreTest-" +
        std::to_wstring(GetCurrentProcessId());
    const BOOL created = CreateDirectoryW(directory.c_str(), nullptr);
    if (!created && GetLastError() != ERROR_ALREADY_EXISTS) {
        std::printf("SKIP: temporary state directory unavailable: path=%ls "
                    "error=%lu\n",
                    directory.c_str(), GetLastError());
        return;
    }
    const std::wstring statePath = directory + L"\\state.ini";
    const std::wstring backupPath = directory + L"\\userchoice.ini";

    mdgeui::AssociationFileState state;
    state.version2 = true;
    state.projectReady = true;
    state.markdownPromptDone = true;
    state.markdownRequested = false;
    state.lastError = 5;
    state.lastRecoveryPromptVersion = 2;
    state.lastRecoveryPromptTime = 123456789;
    const BOOL stateSaved =
        mdgeui::SaveAssociationFileStateAtPath(statePath, state);
    if (!stateSaved) {
        std::printf("state save failed: path=%ls error=%lu exists=%d\n",
                    statePath.c_str(), GetLastError(),
                    GetFileAttributesW(statePath.c_str()) !=
                        INVALID_FILE_ATTRIBUTES);
    }
    Check(stateSaved, "association state store writes a file");
    mdgeui::AssociationFileState loaded;
    Check(mdgeui::LoadAssociationFileStateAtPath(statePath, loaded),
          "association state store reads a file");
    Check(loaded.valid && loaded.version2 && loaded.projectReady &&
              loaded.markdownPromptDone && !loaded.markdownRequested &&
              loaded.lastError == 5 &&
              loaded.lastRecoveryPromptVersion == 2 &&
              loaded.lastRecoveryPromptTime == 123456789,
          "association state store round-trips fields");

    mdgeui::UserChoiceBackup backup;
    backup.exists = true;
    backup.progId = L"VSCode.md";
    backup.hash = L"abc123==";
    Check(mdgeui::SaveUserChoiceBackupFileAtPath(backupPath, backup),
          "UserChoice backup writes a file");
    mdgeui::UserChoiceBackup loadedBackup;
    Check(mdgeui::LoadUserChoiceBackupFileAtPath(backupPath, loadedBackup),
          "UserChoice backup reads a file");
    Check(loadedBackup.exists && loadedBackup.progId == L"VSCode.md" &&
              loadedBackup.hash == L"abc123==",
          "UserChoice backup round-trips fields");

    DeleteFileW(statePath.c_str());
    DeleteFileW((statePath + L".tmp").c_str());
    DeleteFileW(backupPath.c_str());
    DeleteFileW((backupPath + L".tmp").c_str());
    RemoveDirectoryW(directory.c_str());
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::printf("MDGE association tests\n");
    TestLegacyMigration();
    TestDecisionFlow();
    TestOpenWithAndPaths();
    TestSidHandling();
    TestRegistrationValues();
    TestHelperArguments();
    TestRecoveryThrottle();
    TestUserChoiceBackup();
    TestAssociationFileStore();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
