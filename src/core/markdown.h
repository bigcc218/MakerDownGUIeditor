// MDGE Studio - Markdown serialization
#pragma once

#include "model.h"

#include <string>

namespace mdge {

// Serializes the WYSIWYG blocks to CommonMark-compatible Markdown. Project
// metadata (title, author, icon) is deliberately not written into the .md
// file; it belongs to the .mdge project container.
std::wstring DocumentToMarkdown(const Document& document);
std::wstring BlockToMarkdown(const Block& block, int number);
std::wstring EscapeMarkdownInline(const std::wstring& text);

// Parses the supported CommonMark subset used by the visual editor. Unknown
// constructs are preserved as paragraph text rather than rejected. The
// function is intentionally lenient so it can be used while the source pane is
// being typed into.
std::vector<Block> MarkdownToBlocks(const std::wstring& markdown);

}  // namespace mdge
