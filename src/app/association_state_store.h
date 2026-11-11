// Local association state and UserChoice backup fallback.
#pragma once

#include "file_association_core.h"

#include <cstdint>
#include <string>

namespace mdgeui {

struct AssociationFileState {
    bool valid = false;
    bool version2 = false;
    bool projectReady = false;
    bool markdownPromptDone = false;
    bool markdownRequested = false;
    std::uint32_t lastError = 0;
    std::uint32_t lastRecoveryPromptVersion = 0;
    std::uint64_t lastRecoveryPromptTime = 0;
};

std::wstring AssociationStateFilePath();
std::wstring UserChoiceBackupFilePath();

bool LoadAssociationFileState(AssociationFileState& state);
bool SaveAssociationFileState(const AssociationFileState& state);

bool LoadAssociationFileStateAtPath(const std::wstring& path,
                                    AssociationFileState& state);
bool SaveAssociationFileStateAtPath(const std::wstring& path,
                                    const AssociationFileState& state);

bool LoadUserChoiceBackupFile(UserChoiceBackup& backup);
bool SaveUserChoiceBackupFile(const UserChoiceBackup& backup);
bool ClearUserChoiceBackupFile();

bool LoadUserChoiceBackupFileAtPath(const std::wstring& path,
                                    UserChoiceBackup& backup);
bool SaveUserChoiceBackupFileAtPath(const std::wstring& path,
                                    const UserChoiceBackup& backup);

}  // namespace mdgeui
