// MDGE Studio - document model
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mdge {

enum class BlockType : std::uint8_t {
    Paragraph = 0,
    Heading1 = 1,
    Heading2 = 2,
    Heading3 = 3,
    Bullet = 4,
    Numbered = 5,
    Quote = 6,
    Code = 7,
    Image = 8,
    Divider = 9,
    // A GitHub-flavoured task item is a text block with an interactive
    // checkbox. One block represents one task, mirroring bullet/numbered
    // items and keeping the checked state in the project file's block type.
    TaskUnchecked = 10,
    TaskChecked = 11,
};

// Inline style bits. They are stored once per UTF-16 code unit.
enum InlineStyle : std::uint8_t {
    StyleBold = 1u << 0,
    StyleItalic = 1u << 1,
    StyleCode = 1u << 2,
    StyleLink = 1u << 3,
};

struct LinkRun {
    std::size_t start = 0;
    std::size_t length = 0;
    std::wstring href;

    std::size_t end() const { return start + length; }
};

struct Block {
    BlockType type = BlockType::Paragraph;
    std::wstring text;
    // Parallel to text; every character carries its own inline style bits.
    std::vector<std::uint8_t> styles;
    std::vector<LinkRun> links;
    std::wstring imagePath;
    std::wstring caption;
    std::wstring language;
    std::uint64_t revision = 1;
};

struct Document {
    std::wstring title = L"未命名文档";
    std::wstring author;
    std::wstring iconPath;
    std::vector<Block> blocks;
    std::uint64_t created = 0;
    std::uint64_t modified = 0;
};

struct EditorSelection {
    int block = 0;
    std::size_t caret = 0;
    std::size_t anchor = 0;
};

bool IsTextual(BlockType type);
bool IsList(BlockType type);
bool IsTask(BlockType type);
bool IsMedia(BlockType type);

const wchar_t* BlockTypeName(BlockType type);
const wchar_t* BlockTypeShortName(BlockType type);
const wchar_t* BlockTypeHint(BlockType type);

Block MakeBlock(BlockType type);
Document MakeStarterDocument();

// Ensures style/link arrays are inside their valid bounds. Mutations below
// already do this; call it after loading data from disk as well.
void NormalizeBlock(Block& block);
void NormalizeDocument(Document& document);

std::uint8_t StyleAt(const Block& block, std::size_t position);
std::wstring LinkAt(const Block& block, std::size_t position);
bool HasStyleInRange(const Block& block, std::size_t start, std::size_t end,
                     std::uint8_t flag);

// Insert plain text. Links are never extended by typing; a link splits around
// the insertion point. Style bits are repeated for every inserted code unit.
void InsertText(Block& block, std::size_t position, const std::wstring& text,
                std::uint8_t styleBits = 0);

void EraseText(Block& block, std::size_t start, std::size_t end);

void ApplyInlineStyle(Block& block, std::size_t start, std::size_t end,
                      std::uint8_t flag, bool enabled);
void ApplyLink(Block& block, std::size_t start, std::size_t end,
               const std::wstring& href);

// Splits a block at position. The left block keeps the original type. The
// returned right block has the same type and the corresponding right-hand
// text, styles and links. Callers usually change the right block's type.
Block SplitBlock(Block& left, std::size_t position);

// Appends src to dst and clears src. Links of src are shifted by the old
// destination length. Both blocks must be textual.
void MergeBlock(Block& destination, Block& source);

void SetBlockType(Block& block, BlockType type);
void Touch(Block& block);

int CountWords(const Document& document);
int CountCharacters(const Document& document);
std::wstring MakeExcerpt(const std::wstring& text, std::size_t limit);

}  // namespace mdge
