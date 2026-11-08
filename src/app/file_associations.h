// MDGE Studio - first-run file association setup
#pragma once

#include <windows.h>

#include <string>

namespace mdgeui {

// Performs the one-time per-user association setup for .mdge and optionally
// .md. The caller should pass a fully-qualified executable path and a visible
// owner window for the .md prompt.
void InitializeFileAssociations(HWND owner, const std::wstring& executablePath);

}  // namespace mdgeui
