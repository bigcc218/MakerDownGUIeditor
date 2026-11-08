#include "markdown.h"

#include "path_utils.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace mdge {
namespace {

bool IsInlineSpecial(wchar_t ch) {
    switch (ch) {
        case L'\\':
        case L'`':
        case L'*':
        case L'_':
        case L'[':
        case L']':
        case L'<':
        case L'>':
            return true;
        default:
            return false;
    }
}

std::wstring EscapeText(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size() + 8);
    for (wchar_t ch : text) {
        if (IsInlineSpecial(ch)) {
            result.push_back(L'\\');
        }
        result.push_back(ch);
    }
    return result;
}

std::wstring ChooseBacktickFence(const std::wstring& text) {
    std::size_t longest = 0;
    std::size_t current = 0;
    for (wchar_t ch : text) {
        if (ch == L'`') {
            ++current;
            longest = std::max(longest, current);
        } else {
            current = 0;
        }
    }
    return std::wstring(std::max<std::size_t>(1, longest + 1), L'`');
}

std::wstring CodeSpan(const std::wstring& text) {
    const std::wstring fence = ChooseBacktickFence(text);
    const bool pad = !text.empty() &&
                     (text.front() == L'`' || text.back() == L'`' ||
                      text.front() == L' ' || text.back() == L' ');
    if (pad) {
        return fence + L" " + text + L" " + fence;
    }
    return fence + text + fence;
}

std::wstring LinkDestination(const std::wstring& href) {
    if (href.empty()) {
        return {};
    }
    bool needsAngle = false;
    for (wchar_t ch : href) {
        if (iswspace(static_cast<wint_t>(ch)) || ch == L'(' || ch == L')' ||
            ch == L'<' || ch == L'>') {
            needsAngle = true;
            break;
        }
    }
    if (needsAngle) {
        std::wstring escaped;
        escaped.reserve(href.size() + 2);
        escaped.push_back(L'<');
        for (wchar_t ch : href) {
            if (ch == L'>') {
                escaped.push_back(L'\\');
            }
            escaped.push_back(ch);
        }
        escaped.push_back(L'>');
        return escaped;
    }
    return href;
}

bool IsAllWhitespace(const std::wstring& text) {
    for (wchar_t ch : text) {
        if (!iswspace(static_cast<wint_t>(ch))) {
            return false;
        }
    }
    return true;
}

std::wstring DecorateSegment(const std::wstring& text, std::uint8_t style,
                             const std::wstring& href) {
    if (text.empty()) {
        return {};
    }
    const bool bold = (style & StyleBold) != 0;
    const bool italic = (style & StyleItalic) != 0;
    const bool code = (style & StyleCode) != 0;

    std::wstring result = code ? CodeSpan(text) : EscapeText(text);
    if (bold && italic) {
        result = L"***" + result + L"***";
    } else if (bold) {
        result = L"**" + result + L"**";
    } else if (italic) {
        result = L"*" + result + L"*";
    }
    if (!href.empty()) {
        const std::wstring destination = LinkDestination(href);
        if (!destination.empty()) {
            result = L"[" + result + L"](" + destination + L")";
        }
    }
    return result;
}

std::wstring SerializeInline(const Block& block) {
    std::wstring output;
    if (block.text.empty()) {
        return output;
    }
    output.reserve(block.text.size() + 16);

    std::size_t index = 0;
    while (index < block.text.size()) {
        if (block.text[index] == L'\r') {
            ++index;
            continue;
        }
        if (block.text[index] == L'\n') {
            output.push_back(L'\n');
            ++index;
            continue;
        }

        std::wstring href;
        if (index < block.styles.size() && (block.styles[index] & StyleLink) != 0) {
            href = LinkAt(block, index);
        }
        std::uint8_t style = index < block.styles.size()
                                 ? static_cast<std::uint8_t>(block.styles[index] & ~StyleLink)
                                 : 0;
        if (href.empty()) {
            style = static_cast<std::uint8_t>(style & ~StyleLink);
        }

        std::size_t end = index + 1;
        while (end < block.text.size() && block.text[end] != L'\n' &&
               block.text[end] != L'\r') {
            std::wstring nextHref;
            if (end < block.styles.size() && (block.styles[end] & StyleLink) != 0) {
                nextHref = LinkAt(block, end);
            }
            std::uint8_t nextStyle =
                end < block.styles.size()
                    ? static_cast<std::uint8_t>(block.styles[end] & ~StyleLink)
                    : 0;
            if (nextHref.empty()) {
                nextStyle = static_cast<std::uint8_t>(nextStyle & ~StyleLink);
            }
            if (nextStyle != style || nextHref != href) {
                break;
            }
            ++end;
        }

        std::wstring segment = block.text.substr(index, end - index);
        if (IsAllWhitespace(segment)) {
            output += EscapeText(segment);
        } else {
            // Keep edge whitespace outside the emphasis/link markers so
            // CommonMark recognizes the inline style. This also makes the
            // source substantially easier to read.
            std::size_t first = 0;
            while (first < segment.size() &&
                   iswspace(static_cast<wint_t>(segment[first]))) {
                ++first;
            }
            std::size_t last = segment.size();
            while (last > first &&
                   iswspace(static_cast<wint_t>(segment[last - 1]))) {
                --last;
            }
            output += EscapeText(segment.substr(0, first));
            output += DecorateSegment(segment.substr(first, last - first), style, href);
            output += EscapeText(segment.substr(last));
        }
        index = end;
    }
    return output;
}

void EscapeDangerousLineStarts(std::wstring& text) {
    std::wstring result;
    result.reserve(text.size() + 8);
    std::size_t index = 0;
    bool lineStart = true;
    while (index < text.size()) {
        if (lineStart) {
            std::size_t lead = 0;
            while (index + lead < text.size() && text[index + lead] == L' ' && lead < 3) {
                result.push_back(L' ');
                ++lead;
            }
            index += lead;
            if (index < text.size()) {
                const wchar_t first = text[index];
                bool escape = false;
                if (first == L'#' || first == L'>' || first == L'|' || first == L'=') {
                    escape = true;
                } else if ((first == L'-' || first == L'+') &&
                           index + 1 < text.size() &&
                           (text[index + 1] == L' ' || text[index + 1] == L'\t')) {
                    escape = true;
                } else if (iswdigit(static_cast<wint_t>(first))) {
                    std::size_t cursor = index;
                    while (cursor < text.size() &&
                           iswdigit(static_cast<wint_t>(text[cursor]))) {
                        ++cursor;
                    }
                    if (cursor < text.size() &&
                        (text[cursor] == L'.' || text[cursor] == L')') &&
                        cursor + 1 < text.size() && text[cursor + 1] == L' ') {
                        // Escape the punctuation that would start an ordered list.
                        result.append(text.begin() + static_cast<std::ptrdiff_t>(index),
                                      text.begin() + static_cast<std::ptrdiff_t>(cursor));
                        result.push_back(L'\\');
                        result.push_back(text[cursor]);
                        index = cursor + 1;
                        lineStart = false;
                        continue;
                    }
                }
                if (escape) {
                    result.push_back(L'\\');
                }
            }
            lineStart = false;
        }

        if (index >= text.size()) {
            break;
        }
        result.push_back(text[index]);
        if (text[index] == L'\n') {
            lineStart = true;
        }
        ++index;
    }
    text = std::move(result);
}

std::wstring PrefixLines(const std::wstring& text, const std::wstring& firstPrefix,
                         const std::wstring& nextPrefix) {
    std::wstring result;
    result.reserve(text.size() + 8);
    result += firstPrefix;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\n') {
            result.push_back(L'\n');
            if (i + 1 < text.size()) {
                result += nextPrefix;
            }
        } else if (ch != L'\r') {
            result.push_back(ch);
        }
    }
    return result;
}

std::wstring FlattenLines(const std::wstring& text) {
    std::wstring result = text;
    for (wchar_t& ch : result) {
        if (ch == L'\r' || ch == L'\n') {
            ch = L' ';
        }
    }
    return result;
}

std::wstring SanitizeLanguage(const std::wstring& language) {
    std::wstring result;
    for (wchar_t ch : TrimText(language)) {
        if (ch == L'\r' || ch == L'\n') {
            break;
        }
        if (iswalnum(static_cast<wint_t>(ch)) || ch == L'-' || ch == L'+' ||
            ch == L'#' || ch == L'_') {
            result.push_back(ch);
        }
    }
    return result;
}

std::wstring ImageDestination(const std::wstring& path) {
    std::wstring result = ToMarkdownPath(CleanPathInput(path));
    if (result.empty()) {
        return result;
    }
    bool needsAngle = false;
    for (wchar_t ch : result) {
        if (iswspace(static_cast<wint_t>(ch)) || ch == L'(' || ch == L')' ||
            ch == L'<' || ch == L'>') {
            needsAngle = true;
            break;
        }
    }
    if (needsAngle) {
        std::wstring escaped = L"<";
        for (wchar_t ch : result) {
            if (ch == L'>') {
                escaped.push_back(L'\\');
            }
            escaped.push_back(ch);
        }
        escaped.push_back(L'>');
        return escaped;
    }
    return result;
}

std::wstring EscapeAlt(const std::wstring& text) {
    std::wstring result;
    for (wchar_t ch : text) {
        if (ch == L'[' || ch == L']' || ch == L'\\' || ch == L'\r' || ch == L'\n') {
            if (ch == L'\r' || ch == L'\n') {
                result.push_back(L' ');
            } else {
                result.push_back(L'\\');
                result.push_back(ch);
            }
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

struct InlineRun {
    std::wstring text;
    std::uint8_t style = 0;
    std::wstring href;
};

void AppendInlineRun(std::vector<InlineRun>& runs, const std::wstring& text,
                     std::uint8_t style, const std::wstring& href) {
    if (text.empty()) {
        return;
    }
    if (!runs.empty() && runs.back().style == style && runs.back().href == href) {
        runs.back().text += text;
    } else {
        runs.push_back(InlineRun{text, style, href});
    }
}

bool IsEscapedAt(const std::wstring& text, std::size_t position) {
    std::size_t slashes = 0;
    while (position > 0 && text[position - 1] == L'\\') {
        ++slashes;
        --position;
    }
    return (slashes % 2) == 1;
}

bool IsPunctuationForEscape(wchar_t ch) {
    if (ch < 0x21 || ch > 0x7E) {
        return false;
    }
    switch (ch) {
        case L'!':
        case L'"':
        case L'#':
        case L'$':
        case L'%':
        case L'&':
        case L'\'':
        case L'(':
        case L')':
        case L'*':
        case L'+':
        case L',':
        case L'-':
        case L'.':
        case L'/':
        case L':':
        case L';':
        case L'<':
        case L'=':
        case L'>':
        case L'?':
        case L'@':
        case L'[':
        case L'\\':
        case L']':
        case L'^':
        case L'_':
        case L'`':
        case L'{':
        case L'|':
        case L'}':
        case L'~':
            return true;
        default:
            return false;
    }
}

std::size_t FindUnescaped(const std::wstring& text, std::size_t from,
                          std::size_t end, const std::wstring& needle) {
    std::size_t position = from;
    while (position < end) {
        position = text.find(needle, position);
        if (position == std::wstring::npos || position + needle.size() > end) {
            return std::wstring::npos;
        }
        if (!IsEscapedAt(text, position)) {
            return position;
        }
        position += needle.size();
    }
    return std::wstring::npos;
}

std::size_t FindClosingBracket(const std::wstring& text, std::size_t open,
                               std::size_t end) {
    int depth = 1;
    for (std::size_t i = open + 1; i < end; ++i) {
        if (IsEscapedAt(text, i)) {
            continue;
        }
        if (text[i] == L'[') {
            ++depth;
        } else if (text[i] == L']') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::wstring::npos;
}

std::size_t FindLinkDestinationEnd(const std::wstring& text, std::size_t open,
                                   std::size_t end) {
    bool angle = false;
    int parentheses = 0;
    for (std::size_t i = open + 1; i < end; ++i) {
        const wchar_t ch = text[i];
        if (IsEscapedAt(text, i)) {
            continue;
        }
        if (i == open + 1 || (i > 0 && text[i - 1] != L' ' && text[i - 1] != L'\t')) {
            if (ch == L'<' && !angle && parentheses == 0) {
                angle = true;
                continue;
            }
            if (ch == L'>' && angle) {
                angle = false;
                continue;
            }
        }
        if (angle) {
            continue;
        }
        if (ch == L'(') {
            ++parentheses;
        } else if (ch == L')') {
            if (parentheses == 0) {
                return i;
            }
            --parentheses;
        }
    }
    return std::wstring::npos;
}

std::wstring UnescapeInline(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' && i + 1 < text.size() &&
            IsPunctuationForEscape(text[i + 1])) {
            result.push_back(text[i + 1]);
            ++i;
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}

std::wstring TrimLinkDestination(const std::wstring& raw) {
    std::wstring destination = TrimText(raw);
    if (destination.size() >= 2 && destination.front() == L'<' &&
        destination.back() == L'>') {
        destination = destination.substr(1, destination.size() - 2);
    } else {
        // Drop an optional quote-delimited title following the destination.
        const std::size_t space = destination.find_first_of(L" \t");
        if (space != std::wstring::npos) {
            destination.resize(space);
        }
    }
    return UnescapeInline(destination);
}

void ParseInlineInto(const std::wstring& text, std::size_t begin, std::size_t end,
                     std::uint8_t baseStyle, const std::wstring& href,
                     std::vector<InlineRun>& runs, int depth);

void ParseLinkAt(const std::wstring& text, std::size_t position, std::size_t end,
                 std::uint8_t baseStyle, const std::wstring& href,
                 std::vector<InlineRun>& runs, int depth, std::size_t& consumed) {
    consumed = 0;
    const std::size_t closeBracket = FindClosingBracket(text, position, end);
    if (closeBracket == std::wstring::npos || closeBracket + 1 >= end ||
        text[closeBracket + 1] != L'(') {
        return;
    }
    const std::size_t closeParen = FindLinkDestinationEnd(text, closeBracket + 1, end);
    if (closeParen == std::wstring::npos) {
        return;
    }
    const std::wstring destination = TrimLinkDestination(
        text.substr(closeBracket + 2, closeParen - closeBracket - 2));
    ParseInlineInto(text, position + 1, closeBracket, baseStyle,
                    destination.empty() ? href : destination, runs, depth + 1);
    consumed = closeParen + 1 - position;
}

void ParseInlineInto(const std::wstring& text, std::size_t begin, std::size_t end,
                     std::uint8_t baseStyle, const std::wstring& href,
                     std::vector<InlineRun>& runs, int depth) {
    if (depth > 32) {
        AppendInlineRun(runs, text.substr(begin, end - begin), baseStyle, href);
        return;
    }
    std::size_t i = begin;
    while (i < end) {
        const wchar_t ch = text[i];

        if (ch == L'\\' && i + 1 < end &&
            IsPunctuationForEscape(text[i + 1])) {
            AppendInlineRun(runs, std::wstring(1, text[i + 1]), baseStyle, href);
            i += 2;
            continue;
        }

        if (ch == L'`') {
            std::size_t run = 1;
            while (i + run < end && text[i + run] == L'`') {
                ++run;
            }
            const std::wstring fence(run, L'`');
            std::size_t search = i + run;
            std::size_t close = std::wstring::npos;
            while (search < end) {
                const std::size_t candidate = FindUnescaped(text, search, end, fence);
                if (candidate == std::wstring::npos) {
                    break;
                }
                const bool moreBefore = candidate > 0 && text[candidate - 1] == L'`';
                const bool moreAfter =
                    candidate + run < end && text[candidate + run] == L'`';
                if (!moreBefore && !moreAfter) {
                    close = candidate;
                    break;
                }
                search = candidate + run;
            }
            if (close != std::wstring::npos) {
                std::wstring content = text.substr(i + run, close - (i + run));
                for (wchar_t& contentChar : content) {
                    if (contentChar == L'\r' || contentChar == L'\n') {
                        contentChar = L' ';
                    }
                }
                if (content.size() >= 2 && content.front() == L' ' &&
                    content.back() == L' ' && !IsAllWhitespace(content)) {
                    content = content.substr(1, content.size() - 2);
                }
                AppendInlineRun(runs, content,
                                static_cast<std::uint8_t>(baseStyle | StyleCode),
                                href);
                i = close + run;
                continue;
            }
            AppendInlineRun(runs, fence, baseStyle, href);
            i += run;
            continue;
        }

        if (ch == L'*' && i + 2 < end && text[i + 1] == L'*' &&
            text[i + 2] == L'*') {
            const std::size_t close = FindUnescaped(text, i + 3, end, L"***");
            if (close != std::wstring::npos && close > i + 3) {
                ParseInlineInto(
                    text, i + 3, close,
                    static_cast<std::uint8_t>(baseStyle | StyleBold | StyleItalic),
                    href, runs, depth + 1);
                i = close + 3;
                continue;
            }
            AppendInlineRun(runs, L"***", baseStyle, href);
            i += 3;
            continue;
        }

        if (ch == L'*' && i + 1 < end && text[i + 1] == L'*') {
            const std::size_t close = FindUnescaped(text, i + 2, end, L"**");
            if (close != std::wstring::npos && close > i + 2) {
                ParseInlineInto(text, i + 2, close,
                                static_cast<std::uint8_t>(baseStyle | StyleBold),
                                href, runs, depth + 1);
                i = close + 2;
                continue;
            }
            AppendInlineRun(runs, L"**", baseStyle, href);
            i += 2;
            continue;
        }

        if (ch == L'*') {
            std::size_t close = std::wstring::npos;
            for (std::size_t search = i + 1; search < end; ++search) {
                if (text[search] != L'*' || IsEscapedAt(text, search)) {
                    continue;
                }
                if (search + 1 < end && text[search + 1] == L'*') {
                    ++search;
                    continue;
                }
                close = search;
                break;
            }
            if (close != std::wstring::npos && close > i + 1) {
                ParseInlineInto(text, i + 1, close,
                                static_cast<std::uint8_t>(baseStyle | StyleItalic),
                                href, runs, depth + 1);
                i = close + 1;
                continue;
            }
            AppendInlineRun(runs, L"*", baseStyle, href);
            ++i;
            continue;
        }

        if (ch == L'[') {
            std::size_t consumed = 0;
            ParseLinkAt(text, i, end, baseStyle, href, runs, depth, consumed);
            if (consumed > 0) {
                i += consumed;
                continue;
            }
            AppendInlineRun(runs, L"[", baseStyle, href);
            ++i;
            continue;
        }

        AppendInlineRun(runs, std::wstring(1, ch), baseStyle, href);
        ++i;
    }
}

Block MakeParsedInlineBlock(BlockType type, const std::wstring& rawText) {
    Block block = MakeBlock(type);
    std::vector<InlineRun> runs;
    ParseInlineInto(rawText, 0, rawText.size(), 0, L"", runs, 0);
    for (const InlineRun& run : runs) {
        const std::size_t start = block.text.size();
        block.text += run.text;
        const std::uint8_t style =
            static_cast<std::uint8_t>(run.style | (run.href.empty() ? 0 : StyleLink));
        block.styles.insert(block.styles.end(), run.text.size(), style);
        if (!run.href.empty()) {
            block.links.push_back(LinkRun{start, run.text.size(), run.href});
        }
    }
    NormalizeBlock(block);
    return block;
}

bool IsBlankLine(const std::wstring& line) {
    return TrimText(line).empty();
}

std::size_t LeadingSpaces(const std::wstring& line) {
    std::size_t count = 0;
    while (count < line.size() && (line[count] == L' ' || line[count] == L'\t')) {
        ++count;
    }
    return count;
}

struct FenceInfo {
    bool valid = false;
    wchar_t character = L'`';
    std::size_t count = 0;
    std::wstring info;
};

FenceInfo DetectFence(const std::wstring& line) {
    FenceInfo result;
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3 || indent >= line.size()) {
        return result;
    }
    const wchar_t ch = line[indent];
    if (ch != L'`' && ch != L'~') {
        return result;
    }
    std::size_t count = 0;
    while (indent + count < line.size() && line[indent + count] == ch) {
        ++count;
    }
    if (count < 3) {
        return result;
    }
    result.valid = true;
    result.character = ch;
    result.count = count;
    result.info = TrimText(line.substr(indent + count));
    return result;
}

bool IsFenceClose(const std::wstring& line, const FenceInfo& opening) {
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3) {
        return false;
    }
    std::size_t count = 0;
    while (indent + count < line.size() &&
           line[indent + count] == opening.character) {
        ++count;
    }
    if (count < opening.count) {
        return false;
    }
    for (std::size_t i = indent + count; i < line.size(); ++i) {
        if (line[i] != L' ' && line[i] != L'\t') {
            return false;
        }
    }
    return true;
}

struct HeadingInfo {
    bool valid = false;
    int level = 0;
    std::wstring content;
};

HeadingInfo DetectHeading(const std::wstring& line) {
    HeadingInfo result;
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3) {
        return result;
    }
    std::size_t count = 0;
    while (indent + count < line.size() && line[indent + count] == L'#') {
        ++count;
    }
    if (count == 0 || count > 6) {
        return result;
    }
    if (indent + count < line.size() && line[indent + count] != L' ' &&
        line[indent + count] != L'\t') {
        return result;
    }
    result.valid = true;
    result.level = static_cast<int>(count);
    result.content = TrimText(line.substr(indent + count));
    return result;
}

bool DetectThematicBreak(const std::wstring& line) {
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3) {
        return false;
    }
    wchar_t marker = 0;
    std::size_t count = 0;
    for (std::size_t i = indent; i < line.size(); ++i) {
        const wchar_t ch = line[i];
        if (ch == L' ' || ch == L'\t') {
            continue;
        }
        if (ch != L'-' && ch != L'*' && ch != L'_') {
            return false;
        }
        if (marker == 0) {
            marker = ch;
        } else if (marker != ch) {
            return false;
        }
        ++count;
    }
    return count >= 3;
}

struct ListInfo {
    bool valid = false;
    bool ordered = false;
    std::wstring content;
};

struct TaskInfo {
    bool valid = false;
    bool checked = false;
    std::wstring content;
};

// GitHub task-list item: "- [ ] text", "- [x] text". The space inside the
// brackets may be omitted ([]), which makes pasting simpler while still
// exporting canonical GitHub syntax.
TaskInfo DetectTask(const std::wstring& line) {
    TaskInfo result;
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3 || indent >= line.size()) {
        return result;
    }
    std::size_t cursor = indent;
    if (line[cursor] != L'-' && line[cursor] != L'*' && line[cursor] != L'+') {
        return result;
    }
    if (cursor + 1 >= line.size() ||
        (line[cursor + 1] != L' ' && line[cursor + 1] != L'\t')) {
        return result;
    }
    ++cursor;
    while (cursor < line.size() &&
           (line[cursor] == L' ' || line[cursor] == L'\t')) {
        ++cursor;
    }
    if (cursor >= line.size() || line[cursor] != L'[') {
        return result;
    }
    const std::size_t close = line.find(L']', cursor + 1);
    if (close == std::wstring::npos) {
        return result;
    }
    const std::wstring marker = line.substr(cursor + 1, close - cursor - 1);
    if (marker.empty() || marker == L" ") {
        result.checked = false;
    } else if (marker == L"x" || marker == L"X") {
        result.checked = true;
    } else {
        return result;
    }
    cursor = close + 1;
    if (cursor < line.size() && line[cursor] != L' ' && line[cursor] != L'\t') {
        return result;
    }
    while (cursor < line.size() &&
           (line[cursor] == L' ' || line[cursor] == L'\t')) {
        ++cursor;
    }
    result.valid = true;
    result.content = line.substr(cursor);
    return result;
}

ListInfo DetectList(const std::wstring& line) {
    ListInfo result;
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3 || indent >= line.size()) {
        return result;
    }
    std::size_t cursor = indent;
    if (line[cursor] == L'-' || line[cursor] == L'*' || line[cursor] == L'+') {
        if (cursor + 1 >= line.size() ||
            (line[cursor + 1] != L' ' && line[cursor + 1] != L'\t')) {
            return result;
        }
        result.valid = true;
        ++cursor;
    } else if (iswdigit(static_cast<wint_t>(line[cursor]))) {
        const std::size_t digitStart = cursor;
        while (cursor < line.size() &&
               iswdigit(static_cast<wint_t>(line[cursor]))) {
            ++cursor;
        }
        if (cursor == digitStart || cursor >= line.size() ||
            (line[cursor] != L'.' && line[cursor] != L')')) {
            return result;
        }
        ++cursor;
        if (cursor >= line.size() ||
            (line[cursor] != L' ' && line[cursor] != L'\t')) {
            return result;
        }
        result.valid = true;
        result.ordered = true;
    } else {
        return result;
    }
    while (cursor < line.size() &&
           (line[cursor] == L' ' || line[cursor] == L'\t')) {
        ++cursor;
    }
    result.content = line.substr(cursor);
    return result;
}

bool DetectQuote(const std::wstring& line, std::wstring& content) {
    const std::size_t indent = LeadingSpaces(line);
    if (indent > 3 || indent >= line.size() || line[indent] != L'>') {
        return false;
    }
    std::size_t cursor = indent + 1;
    if (cursor < line.size() && (line[cursor] == L' ' || line[cursor] == L'\t')) {
        ++cursor;
    }
    content = line.substr(cursor);
    return true;
}

bool DetectStandaloneImage(const std::wstring& line, std::wstring& path,
                           std::wstring& alt) {
    const std::wstring trimmed = TrimText(line);
    if (trimmed.size() < 5 || trimmed[0] != L'!' || trimmed[1] != L'[') {
        return false;
    }
    const std::size_t closeBracket = FindClosingBracket(trimmed, 1, trimmed.size());
    if (closeBracket == std::wstring::npos || closeBracket + 1 >= trimmed.size() ||
        trimmed[closeBracket + 1] != L'(') {
        return false;
    }
    const std::size_t closeParen =
        FindLinkDestinationEnd(trimmed, closeBracket + 1, trimmed.size());
    if (closeParen == std::wstring::npos) {
        return false;
    }
    for (std::size_t i = closeParen + 1; i < trimmed.size(); ++i) {
        if (trimmed[i] != L' ' && trimmed[i] != L'\t') {
            return false;
        }
    }
    alt = UnescapeInline(trimmed.substr(2, closeBracket - 2));
    path = TrimLinkDestination(
        trimmed.substr(closeBracket + 2, closeParen - closeBracket - 2));
    return !path.empty();
}

bool StartsBlockConstruct(const std::wstring& line) {
    if (IsBlankLine(line)) {
        return true;
    }
    if (DetectFence(line).valid || DetectHeading(line).valid ||
        DetectThematicBreak(line)) {
        return true;
    }
    if (DetectTask(line).valid || DetectList(line).valid) {
        return true;
    }
    std::wstring quoteContent;
    if (DetectQuote(line, quoteContent)) {
        return true;
    }
    std::wstring imagePath;
    std::wstring imageAlt;
    if (DetectStandaloneImage(line, imagePath, imageAlt)) {
        return true;
    }
    return false;
}

std::vector<std::wstring> SplitLines(const std::wstring& source) {
    std::vector<std::wstring> lines;
    std::size_t start = 0;
    while (start <= source.size()) {
        const std::size_t newline = source.find(L'\n', start);
        std::wstring line = source.substr(
            start, newline == std::wstring::npos ? std::wstring::npos
                                                 : newline - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (newline == std::wstring::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

}  // namespace

std::wstring EscapeMarkdownInline(const std::wstring& text) {
    return EscapeText(text);
}

std::wstring BlockToMarkdown(const Block& block, int number) {
    switch (block.type) {
        case BlockType::Paragraph: {
            std::wstring content = SerializeInline(block);
            EscapeDangerousLineStarts(content);
            return content;
        }
        case BlockType::Heading1:
        case BlockType::Heading2:
        case BlockType::Heading3: {
            const int level = block.type == BlockType::Heading1
                                  ? 1
                                  : (block.type == BlockType::Heading2 ? 2 : 3);
            std::wstring content = FlattenLines(SerializeInline(block));
            return std::wstring(static_cast<std::size_t>(level), L'#') + L" " + content;
        }
        case BlockType::Bullet: {
            std::wstring content = SerializeInline(block);
            EscapeDangerousLineStarts(content);
            return PrefixLines(content, L"- ", L"  ");
        }
        case BlockType::Numbered: {
            std::wstring content = SerializeInline(block);
            EscapeDangerousLineStarts(content);
            return PrefixLines(content, std::to_wstring(number) + L". ", L"   ");
        }
        case BlockType::TaskUnchecked:
        case BlockType::TaskChecked: {
            std::wstring content = SerializeInline(block);
            EscapeDangerousLineStarts(content);
            const std::wstring marker =
                block.type == BlockType::TaskChecked ? L"- [x] " : L"- [ ] ";
            return PrefixLines(content, marker, L"    ");
        }
        case BlockType::Quote: {
            std::wstring content = SerializeInline(block);
            EscapeDangerousLineStarts(content);
            return PrefixLines(content, L"> ", L"> ");
        }
        case BlockType::Code: {
            std::size_t longest = 0;
            std::size_t current = 0;
            for (wchar_t ch : block.text) {
                if (ch == L'`') {
                    ++current;
                    longest = std::max(longest, current);
                } else {
                    current = 0;
                }
            }
            const std::wstring fence(std::max<std::size_t>(3, longest + 1), L'`');
            std::wstring result = fence;
            const std::wstring language = SanitizeLanguage(block.language);
            if (!language.empty()) {
                result += language;
            }
            result += L"\n";
            std::wstring code = block.text;
            for (wchar_t& ch : code) {
                if (ch == L'\r') {
                    ch = L'\n';
                }
            }
            result += code;
            if (!code.empty() && code.back() != L'\n') {
                result += L"\n";
            }
            result += fence;
            return result;
        }
        case BlockType::Image: {
            const std::wstring alt = EscapeAlt(
                block.caption.empty() ? FileStem(block.imagePath) : block.caption);
            const std::wstring destination = ImageDestination(block.imagePath);
            if (destination.empty()) {
                return L"![" + alt + L"]()";
            }
            std::wstring result = L"![" + alt + L"](" + destination + L")";
            if (!block.caption.empty()) {
                // Keep the visual caption as an italic line below the image.
                std::wstring caption = SerializeInline(block);
                if (!caption.empty()) {
                    result += L"\n\n*" + caption + L"*";
                }
            }
            return result;
        }
        case BlockType::Divider:
            return L"---";
    }
    return {};
}

std::wstring DocumentToMarkdown(const Document& document) {
    std::wstring result;
    int numbered = 0;
    BlockType previous = BlockType::Paragraph;
    bool hasPrevious = false;

    for (std::size_t i = 0; i < document.blocks.size(); ++i) {
        const Block& block = document.blocks[i];
        if (block.type == BlockType::Numbered) {
            numbered = (hasPrevious && previous == BlockType::Numbered) ? numbered + 1 : 1;
        } else {
            numbered = 0;
        }
        const std::wstring part = BlockToMarkdown(block, numbered == 0 ? 1 : numbered);
        if (i != 0) {
            const bool tightBullet =
                previous == BlockType::Bullet && block.type == BlockType::Bullet;
            const bool tightNumbered =
                previous == BlockType::Numbered && block.type == BlockType::Numbered;
            const bool tightTask =
                IsTask(previous) && IsTask(block.type);
            result += (tightBullet || tightNumbered || tightTask) ? L"\n" : L"\n\n";
        }
        result += part;
        previous = block.type;
        hasPrevious = true;
    }
    if (!result.empty()) {
        result += L"\n";
    }
    return result;
}

std::vector<Block> MarkdownToBlocks(const std::wstring& markdown) {
    const std::vector<std::wstring> lines = SplitLines(markdown);
    std::vector<Block> blocks;
    std::size_t i = 0;

    while (i < lines.size()) {
        if (IsBlankLine(lines[i])) {
            ++i;
            continue;
        }

        const FenceInfo fence = DetectFence(lines[i]);
        if (fence.valid) {
            Block code = MakeBlock(BlockType::Code);
            std::wstring language = TrimText(fence.info);
            if (language.size() > 80) {
                language.resize(80);
            }
            code.language = std::move(language);
            ++i;
            std::wstring content;
            bool firstLine = true;
            while (i < lines.size()) {
                if (IsFenceClose(lines[i], fence)) {
                    ++i;
                    break;
                }
                if (!firstLine) {
                    content += L'\n';
                }
                content += lines[i];
                firstLine = false;
                ++i;
            }
            code.text = std::move(content);
            code.styles.assign(code.text.size(), 0);
            blocks.push_back(std::move(code));
            continue;
        }

        const HeadingInfo heading = DetectHeading(lines[i]);
        if (heading.valid) {
            BlockType type = BlockType::Heading3;
            if (heading.level == 1) {
                type = BlockType::Heading1;
            } else if (heading.level == 2) {
                type = BlockType::Heading2;
            }
            blocks.push_back(MakeParsedInlineBlock(type, heading.content));
            ++i;
            continue;
        }

        if (DetectThematicBreak(lines[i])) {
            blocks.push_back(MakeBlock(BlockType::Divider));
            ++i;
            continue;
        }

        std::wstring imagePath;
        std::wstring imageAlt;
        if (DetectStandaloneImage(lines[i], imagePath, imageAlt)) {
            Block image = MakeBlock(BlockType::Image);
            image.imagePath = imagePath;
            image.caption = imageAlt;
            blocks.push_back(std::move(image));
            ++i;
            continue;
        }

        const TaskInfo task = DetectTask(lines[i]);
        if (task.valid) {
            std::wstring itemText = task.content;
            ++i;
            while (i < lines.size() && !IsBlankLine(lines[i]) &&
                   !StartsBlockConstruct(lines[i])) {
                const std::size_t indent = LeadingSpaces(lines[i]);
                if (indent < 2) {
                    break;
                }
                const std::size_t strip = std::min<std::size_t>(4, indent);
                itemText += L'\n';
                itemText += lines[i].substr(strip);
                ++i;
            }
            blocks.push_back(MakeParsedInlineBlock(
                task.checked ? BlockType::TaskChecked : BlockType::TaskUnchecked,
                itemText));
            continue;
        }

        const ListInfo list = DetectList(lines[i]);
        if (list.valid) {
            std::wstring itemText = list.content;
            const std::size_t continuationStrip = list.ordered ? 3 : 2;
            ++i;
            while (i < lines.size() && !IsBlankLine(lines[i]) &&
                   !StartsBlockConstruct(lines[i])) {
                const std::size_t indent = LeadingSpaces(lines[i]);
                if (indent < 2) {
                    // A non-indented line belongs to a new paragraph rather
                    // than the preceding list item.
                    break;
                }
                const std::size_t strip = std::min(continuationStrip, indent);
                itemText += L'\n';
                itemText += lines[i].substr(strip);
                ++i;
            }
            blocks.push_back(MakeParsedInlineBlock(
                list.ordered ? BlockType::Numbered : BlockType::Bullet, itemText));
            continue;
        }

        std::wstring quoteContent;
        if (DetectQuote(lines[i], quoteContent)) {
            std::wstring quoteText = quoteContent;
            ++i;
            while (i < lines.size()) {
                std::wstring nextQuote;
                if (!DetectQuote(lines[i], nextQuote)) {
                    break;
                }
                quoteText += L'\n';
                quoteText += nextQuote;
                ++i;
            }
            blocks.push_back(MakeParsedInlineBlock(BlockType::Quote, quoteText));
            continue;
        }

        // Paragraph: gather consecutive ordinary lines.
        std::wstring paragraph;
        bool firstLine = true;
        while (i < lines.size() && !IsBlankLine(lines[i]) &&
               !StartsBlockConstruct(lines[i])) {
            if (!firstLine) {
                paragraph += L'\n';
            }
            paragraph += lines[i];
            firstLine = false;
            ++i;
        }
        if (firstLine) {
            // Defensive guard against an unrecognized special line causing no
            // progress. Treat it as literal text.
            paragraph = lines[i];
            ++i;
        }
        blocks.push_back(MakeParsedInlineBlock(BlockType::Paragraph, paragraph));
    }

    return blocks;
}

}  // namespace mdge
