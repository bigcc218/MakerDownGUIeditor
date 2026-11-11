// Diagnostics and local persistence helpers for file-association setup.
#pragma once

#include <string>

namespace mdgeui {

std::wstring AssociationDataDirectory();
std::wstring AssociationLogPath();
void AppendAssociationLog(const std::wstring& message);

bool IsHuorongDetected();
std::wstring HuorongInstallDirectory();
bool OpenHuorong();

bool CopyTextToClipboard(const std::wstring& text);
std::wstring BuildAssociationDiagnosticSummary(const std::wstring& executablePath);

}  // namespace mdgeui
