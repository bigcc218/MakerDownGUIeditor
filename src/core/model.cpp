#include "model.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <utility>

namespace mdge {
namespace {

void SyncLinkStyles(Block& block) {
    for (std::uint8_t& style : block.styles) {
        style = static_cast<std::uint8_t>(style & ~StyleLink);
    }
    for (const LinkRun& link : block.links) {
        const std::size_t end = std::min(link.end(), block.styles.size());
        for (std::size_t i = link.start; i < end; ++i) {
            block.styles[i] = static_cast<std::uint8_t>(block.styles[i] | StyleLink);
        }
    }
}

void NormalizeLinks(Block& block) {
    const std::size_t textSize = block.text.size();
    std::vector<LinkRun> result;
    result.reserve(block.links.size());

    for (const LinkRun& original : block.links) {
        if (original.length == 0 || original.start >= textSize || original.href.empty()) {
            continue;
        }
        LinkRun link = original;
        link.length = std::min(link.length, textSize - link.start);
        if (link.length == 0) {
            continue;
        }
        result.push_back(std::move(link));
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const LinkRun& a, const LinkRun& b) {
                         if (a.start != b.start) {
                             return a.start < b.start;
                         }
                         return a.end() < b.end();
                     });

    std::vector<LinkRun> merged;
    for (LinkRun& link : result) {
        if (!merged.empty()) {
            LinkRun& previous = merged.back();
            if (link.start < previous.end()) {
                // Overlap from malformed input: keep the first run and trim the
                // overlapping tail of the following one.
                if (link.end() <= previous.end()) {
                    continue;
                }
                const std::size_t trim = previous.end() - link.start;
                link.start += trim;
                link.length -= trim;
            }
        }
        if (!merged.empty() && merged.back().end() == link.start &&
            merged.back().href == link.href) {
            merged.back().length += link.length;
        } else {
            merged.push_back(std::move(link));
        }
    }
    block.links = std::move(merged);
}

void AdjustLinksForInsert(Block& block, std::size_t position, std::size_t count) {
    if (count == 0) {
        return;
    }
    std::vector<LinkRun> adjusted;
    adjusted.reserve(block.links.size() + 1);
    for (const LinkRun& link : block.links) {
        if (position <= link.start) {
            LinkRun moved = link;
            moved.start += count;
            adjusted.push_back(std::move(moved));
        } else if (position >= link.end()) {
            adjusted.push_back(link);
        } else {
            // Typing inside a link creates a small gap so the new text is not
            // silently absorbed into the hyperlink.
            const std::size_t leftLength = position - link.start;
            adjusted.push_back(LinkRun{link.start, leftLength, link.href});
            const std::size_t rightStart = position + count;
            const std::size_t rightLength = link.end() - position;
            adjusted.push_back(LinkRun{rightStart, rightLength, link.href});
        }
    }
    block.links = std::move(adjusted);
    NormalizeLinks(block);
    SyncLinkStyles(block);
}

void AdjustLinksForErase(Block& block, std::size_t start, std::size_t end) {
    if (start >= end) {
        return;
    }
    std::vector<LinkRun> adjusted;
    adjusted.reserve(block.links.size() + 1);
    const std::size_t removed = end - start;
    for (const LinkRun& link : block.links) {
        if (link.end() <= start) {
            adjusted.push_back(link);
            continue;
        }
        if (link.start >= end) {
            LinkRun moved = link;
            moved.start -= removed;
            adjusted.push_back(std::move(moved));
            continue;
        }
        if (link.start < start) {
            adjusted.push_back(LinkRun{link.start, start - link.start, link.href});
        }
        if (link.end() > end) {
            adjusted.push_back(LinkRun{start, link.end() - end, link.href});
        }
    }
    block.links = std::move(adjusted);
    NormalizeLinks(block);
    SyncLinkStyles(block);
}

bool IsCjkCodePoint(std::uint32_t cp) {
    return (cp >= 0x3040 && cp <= 0x30FF) ||   // kana
           (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK ext A
           (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK unified
           (cp >= 0xAC00 && cp <= 0xD7AF) ||   // Hangul
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility
           (cp >= 0x20000 && cp <= 0x2FA1F);   // CJK extensions B-F
}

bool IsWordCodePoint(std::uint32_t cp) {
    if (cp < 128) {
        return (cp >= L'0' && cp <= L'9') || (cp >= L'a' && cp <= L'z') ||
               (cp >= L'A' && cp <= L'Z') || cp == L'_';
    }
    return std::iswalnum(static_cast<wint_t>(cp)) != 0;
}

std::vector<std::uint32_t> CodePoints(const std::wstring& text) {
    std::vector<std::uint32_t> points;
    points.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];
        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < text.size()) {
            const wchar_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                const std::uint32_t cp =
                    0x10000u + ((static_cast<std::uint32_t>(ch) - 0xD800u) << 10u) +
                    (static_cast<std::uint32_t>(low) - 0xDC00u);
                points.push_back(cp);
                ++i;
                continue;
            }
        }
        points.push_back(static_cast<std::uint32_t>(ch));
    }
    return points;
}

}  // namespace

bool IsTextual(BlockType type) {
    switch (type) {
        case BlockType::Paragraph:
        case BlockType::Heading1:
        case BlockType::Heading2:
        case BlockType::Heading3:
        case BlockType::Bullet:
        case BlockType::Numbered:
        case BlockType::Quote:
        case BlockType::Code:
        case BlockType::TaskUnchecked:
        case BlockType::TaskChecked:
            return true;
        case BlockType::Image:
        case BlockType::Divider:
            return false;
    }
    return false;
}

bool IsList(BlockType type) {
    return type == BlockType::Bullet || type == BlockType::Numbered ||
           IsTask(type);
}

bool IsTask(BlockType type) {
    return type == BlockType::TaskUnchecked || type == BlockType::TaskChecked;
}

bool IsMedia(BlockType type) {
    return type == BlockType::Image || type == BlockType::Divider;
}

const wchar_t* BlockTypeName(BlockType type) {
    switch (type) {
        case BlockType::Paragraph: return L"正文";
        case BlockType::Heading1: return L"一级标题";
        case BlockType::Heading2: return L"二级标题";
        case BlockType::Heading3: return L"三级标题";
        case BlockType::Bullet: return L"项目符号";
        case BlockType::Numbered: return L"编号列表";
        case BlockType::Quote: return L"引用";
        case BlockType::Code: return L"代码块";
        case BlockType::Image: return L"图片";
        case BlockType::Divider: return L"分隔线";
        case BlockType::TaskUnchecked:
        case BlockType::TaskChecked: return L"任务清单";
    }
    return L"正文";
}

const wchar_t* BlockTypeShortName(BlockType type) {
    switch (type) {
        case BlockType::Paragraph: return L"P";
        case BlockType::Heading1: return L"H1";
        case BlockType::Heading2: return L"H2";
        case BlockType::Heading3: return L"H3";
        case BlockType::Bullet: return L"LI";
        case BlockType::Numbered: return L"1.";
        case BlockType::Quote: return L"Q";
        case BlockType::Code: return L"</>";
        case BlockType::Image: return L"IMG";
        case BlockType::Divider: return L"HR";
        case BlockType::TaskUnchecked:
        case BlockType::TaskChecked: return L"TASK";
    }
    return L"P";
}

const wchar_t* BlockTypeHint(BlockType type) {
    switch (type) {
        case BlockType::Paragraph: return L"普通段落文字";
        case BlockType::Heading1: return L"页面主标题";
        case BlockType::Heading2: return L"章节标题";
        case BlockType::Heading3: return L"小节标题";
        case BlockType::Bullet: return L"无序要点";
        case BlockType::Numbered: return L"有序步骤";
        case BlockType::Quote: return L"引言与摘录";
        case BlockType::Code: return L"等宽代码片段";
        case BlockType::Image: return L"插入本地图片";
        case BlockType::Divider: return L"视觉分隔";
        case BlockType::TaskUnchecked: return L"可勾选的任务项";
        case BlockType::TaskChecked: return L"已完成任务项";
    }
    return L"文字块";
}

Block MakeBlock(BlockType type) {
    Block block;
    block.type = type;
    block.revision = 1;
    if (type == BlockType::Image) {
        block.caption.clear();
    }
    return block;
}

Document MakeStarterDocument() {
    Document document;
    document.title = L"未命名文档";
    document.blocks.push_back(MakeBlock(BlockType::Paragraph));
    return document;
}

void NormalizeBlock(Block& block) {
    if (block.styles.size() != block.text.size()) {
        block.styles.resize(block.text.size(), 0);
    }
    NormalizeLinks(block);
    SyncLinkStyles(block);
}

void NormalizeDocument(Document& document) {
    for (Block& block : document.blocks) {
        NormalizeBlock(block);
    }
}

std::uint8_t StyleAt(const Block& block, std::size_t position) {
    if (position >= block.styles.size()) {
        return 0;
    }
    return block.styles[position];
}

std::wstring LinkAt(const Block& block, std::size_t position) {
    for (const LinkRun& link : block.links) {
        if (position >= link.start && position < link.end()) {
            return link.href;
        }
    }
    return {};
}

bool HasStyleInRange(const Block& block, std::size_t start, std::size_t end,
                     std::uint8_t flag) {
    if (block.text.empty() || start >= end) {
        return false;
    }
    end = std::min(end, block.styles.size());
    for (std::size_t i = start; i < end; ++i) {
        if ((block.styles[i] & flag) == 0) {
            return false;
        }
    }
    return true;
}

void Touch(Block& block) {
    ++block.revision;
    if (block.revision == 0) {
        block.revision = 1;
    }
}

void InsertText(Block& block, std::size_t position, const std::wstring& text,
                std::uint8_t styleBits) {
    if (text.empty() || !IsTextual(block.type)) {
        return;
    }
    if (block.styles.size() != block.text.size()) {
        block.styles.resize(block.text.size(), 0);
    }
    position = std::min(position, block.text.size());
    styleBits = static_cast<std::uint8_t>(styleBits & (StyleBold | StyleItalic | StyleCode));
    block.text.insert(position, text);
    block.styles.insert(block.styles.begin() + static_cast<std::ptrdiff_t>(position),
                        text.size(), styleBits);
    AdjustLinksForInsert(block, position, text.size());
    SyncLinkStyles(block);
    Touch(block);
}

void EraseText(Block& block, std::size_t start, std::size_t end) {
    if (!IsTextual(block.type) || block.text.empty()) {
        return;
    }
    if (block.styles.size() != block.text.size()) {
        block.styles.resize(block.text.size(), 0);
    }
    start = std::min(start, block.text.size());
    end = std::min(end, block.text.size());
    if (start >= end) {
        return;
    }
    block.text.erase(start, end - start);
    block.styles.erase(block.styles.begin() + static_cast<std::ptrdiff_t>(start),
                       block.styles.begin() + static_cast<std::ptrdiff_t>(end));
    AdjustLinksForErase(block, start, end);
    SyncLinkStyles(block);
    Touch(block);
}

void ApplyInlineStyle(Block& block, std::size_t start, std::size_t end,
                      std::uint8_t flag, bool enabled) {
    if (!IsTextual(block.type) || block.text.empty()) {
        return;
    }
    flag = static_cast<std::uint8_t>(flag & (StyleBold | StyleItalic | StyleCode));
    if (flag == 0) {
        return;
    }
    if (block.styles.size() != block.text.size()) {
        block.styles.resize(block.text.size(), 0);
    }
    start = std::min(start, block.text.size());
    end = std::min(end, block.text.size());
    if (start >= end) {
        return;
    }
    for (std::size_t i = start; i < end; ++i) {
        if (enabled) {
            block.styles[i] = static_cast<std::uint8_t>(block.styles[i] | flag);
        } else {
            block.styles[i] = static_cast<std::uint8_t>(block.styles[i] & ~flag);
        }
    }
    Touch(block);
}

void ApplyLink(Block& block, std::size_t start, std::size_t end,
               const std::wstring& href) {
    if (!IsTextual(block.type) || block.text.empty()) {
        return;
    }
    if (block.styles.size() != block.text.size()) {
        block.styles.resize(block.text.size(), 0);
    }
    start = std::min(start, block.text.size());
    end = std::min(end, block.text.size());
    if (start >= end) {
        return;
    }

    std::vector<LinkRun> adjusted;
    adjusted.reserve(block.links.size() + 1);
    for (const LinkRun& link : block.links) {
        if (link.end() <= start || link.start >= end) {
            adjusted.push_back(link);
            continue;
        }
        if (link.start < start) {
            adjusted.push_back(LinkRun{link.start, start - link.start, link.href});
        }
        if (link.end() > end) {
            adjusted.push_back(LinkRun{end, link.end() - end, link.href});
        }
    }
    if (!href.empty()) {
        adjusted.push_back(LinkRun{start, end - start, href});
    }
    block.links = std::move(adjusted);
    NormalizeLinks(block);
    SyncLinkStyles(block);
    Touch(block);
}

Block SplitBlock(Block& left, std::size_t position) {
    Block right;
    if (!IsTextual(left.type)) {
        return right;
    }
    if (left.styles.size() != left.text.size()) {
        left.styles.resize(left.text.size(), 0);
    }
    position = std::min(position, left.text.size());

    right.type = left.type;
    right.language = left.language;
    right.imagePath = left.imagePath;
    right.caption = left.caption;
    right.text = left.text.substr(position);
    right.styles.assign(left.styles.begin() + static_cast<std::ptrdiff_t>(position),
                        left.styles.end());

    const std::size_t oldSize = left.text.size();
    for (const LinkRun& link : left.links) {
        const std::size_t overlapStart = std::max(link.start, position);
        const std::size_t overlapEnd = std::min(link.end(), oldSize);
        if (overlapStart < overlapEnd) {
            right.links.push_back(LinkRun{overlapStart - position,
                                          overlapEnd - overlapStart, link.href});
        }
    }

    left.text.resize(position);
    left.styles.resize(position);
    for (LinkRun& link : left.links) {
        if (link.start >= position) {
            link.length = 0;
        } else if (link.end() > position) {
            link.length = position - link.start;
        }
    }

    left.links.erase(
        std::remove_if(left.links.begin(), left.links.end(),
                       [](const LinkRun& link) { return link.length == 0; }),
        left.links.end());

    NormalizeBlock(left);
    NormalizeBlock(right);
    Touch(left);
    Touch(right);
    return right;
}

void MergeBlock(Block& destination, Block& source) {
    if (!IsTextual(destination.type) || !IsTextual(source.type)) {
        return;
    }
    if (destination.styles.size() != destination.text.size()) {
        destination.styles.resize(destination.text.size(), 0);
    }
    if (source.styles.size() != source.text.size()) {
        source.styles.resize(source.text.size(), 0);
    }
    const std::size_t offset = destination.text.size();
    destination.text += source.text;
    destination.styles.insert(destination.styles.end(), source.styles.begin(),
                              source.styles.end());
    for (const LinkRun& link : source.links) {
        destination.links.push_back(
            LinkRun{link.start + offset, link.length, link.href});
    }
    NormalizeBlock(destination);
    Touch(destination);

    source.text.clear();
    source.styles.clear();
    source.links.clear();
    NormalizeBlock(source);
    Touch(source);
}

void SetBlockType(Block& block, BlockType type) {
    if (!IsTextual(type) || !IsTextual(block.type)) {
        return;
    }
    if (block.type != type) {
        block.type = type;
        Touch(block);
    }
}

int CountWords(const Document& document) {
    int words = 0;
    for (const Block& block : document.blocks) {
        if (!IsTextual(block.type)) {
            continue;
        }
        bool inLatinWord = false;
        for (std::uint32_t cp : CodePoints(block.text)) {
            if (IsCjkCodePoint(cp)) {
                ++words;
                inLatinWord = false;
            } else if (IsWordCodePoint(cp)) {
                if (!inLatinWord) {
                    ++words;
                    inLatinWord = true;
                }
            } else {
                inLatinWord = false;
            }
        }
    }
    return words;
}

int CountCharacters(const Document& document) {
    int count = 0;
    for (const Block& block : document.blocks) {
        if (!IsTextual(block.type)) {
            continue;
        }
        count += static_cast<int>(CodePoints(block.text).size());
    }
    return count;
}

std::wstring MakeExcerpt(const std::wstring& text, std::size_t limit) {
    std::wstring result;
    result.reserve(std::min(limit, text.size()));
    bool pendingSpace = false;
    for (wchar_t ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t' || ch == L' ') {
            if (!result.empty()) {
                pendingSpace = true;
            }
            continue;
        }
        if (pendingSpace) {
            if (result.size() + 1 >= limit) {
                break;
            }
            result.push_back(L' ');
            pendingSpace = false;
        }
        if (result.size() >= limit) {
            break;
        }
        result.push_back(ch);
    }
    if (result.size() >= limit || result.size() < text.size()) {
        if (result.size() > limit) {
            result.resize(limit);
        }
        if (result.size() >= 3) {
            result.replace(result.size() - 3, 3, L"...");
        } else {
            result = L"...";
        }
    }
    return result;
}

}  // namespace mdge
