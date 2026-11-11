// MDGE Studio - file association setup and recovery.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace mdgeui {

// Hidden/elevated registration mode. `arguments` excludes the executable
// path. Returns the exit code documented in README/file_association_core.h.
int RunFileAssociationHelper(const std::vector<std::wstring>& arguments);

// Runs the first-launch association setup. Safe to call multiple times; the
// persisted state decides which steps still need to run.
void InitializeFileAssociations(HWND owner, const std::wstring& executablePath);

// Explicit user-requested repair from the application UI. Unlike the startup
// flow it may ask about .md again and always exposes recovery for errors.
void RepairFileAssociations(HWND owner, const std::wstring& executablePath);

}  // namespace mdgeui
