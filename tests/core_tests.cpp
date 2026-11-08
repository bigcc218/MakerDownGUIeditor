#include "../src/core/file_utils.h"
#include "../src/core/markdown.h"
#include "../src/core/model.h"
#include "../src/core/path_utils.h"
#include "../src/core/project_io.h"
#include "../src/core/utf8.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL  %s\n", what);
    }
}

bool Contains(const std::wstring& haystack, const std::wstring& needle) {
    return haystack.find(needle) != std::wstring::npos;
}

bool BlocksEqual(const mdge::Block& a, const mdge::Block& b) {
    if (a.type != b.type || a.text != b.text || a.styles != b.styles ||
        a.imagePath != b.imagePath || a.caption != b.caption ||
        a.language != b.language || a.links.size() != b.links.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.links.size(); ++i) {
        if (a.links[i].start != b.links[i].start ||
            a.links[i].length != b.links[i].length ||
            a.links[i].href != b.links[i].href) {
            return false;
        }
    }
    return true;
}

bool DocumentsEqual(const mdge::Document& a, const mdge::Document& b) {
    if (a.title != b.title || a.author != b.author || a.iconPath != b.iconPath ||
        a.created != b.created || a.modified != b.modified ||
        a.blocks.size() != b.blocks.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.blocks.size(); ++i) {
        if (!BlocksEqual(a.blocks[i], b.blocks[i])) {
            return false;
        }
    }
    return true;
}

mdge::Document MakeRichDocument() {
    mdge::Document document;
    document.title = L"视觉写作测试";
    document.author = L"MDGE Studio";
    document.iconPath = L"assets/mdge-mark.png";
    document.created = 1700000000ull;
    document.modified = 1700000123ull;

    mdge::Block title = mdge::MakeBlock(mdge::BlockType::Heading1);
    title.text = L"让想法，轻松成文";
    document.blocks.push_back(title);

    mdge::Block paragraph = mdge::MakeBlock(mdge::BlockType::Paragraph);
    paragraph.text = L"这是一段包含粗体、斜体、行内代码与链接的文字。";
    mdge::InsertText(paragraph, paragraph.text.size(), L" Bold.", mdge::StyleBold);
    mdge::InsertText(paragraph, paragraph.text.size(), L" Italic.", mdge::StyleItalic);
    mdge::InsertText(paragraph, paragraph.text.size(), L" code", mdge::StyleCode);
    mdge::ApplyLink(paragraph, 5, 10, L"https://example.com/文档");
    document.blocks.push_back(paragraph);

    mdge::Block bullet = mdge::MakeBlock(mdge::BlockType::Bullet);
    bullet.text = L"拖动左侧内容块\n直接输入文字";
    document.blocks.push_back(bullet);

    mdge::Block numbered = mdge::MakeBlock(mdge::BlockType::Numbered);
    numbered.text = L"第一步";
    document.blocks.push_back(numbered);
    numbered = mdge::MakeBlock(mdge::BlockType::Numbered);
    numbered.text = L"第二步";
    document.blocks.push_back(numbered);

    mdge::Block quote = mdge::MakeBlock(mdge::BlockType::Quote);
    quote.text = L"少一点格式负担，多一点表达本身。";
    document.blocks.push_back(quote);

    mdge::Block code = mdge::MakeBlock(mdge::BlockType::Code);
    code.text = L"#include <iostream>\nint main() { return 0; }";
    code.language = L"cpp";
    document.blocks.push_back(code);

    mdge::Block image = mdge::MakeBlock(mdge::BlockType::Image);
    image.imagePath = L"assets/cover image.png";
    image.caption = L"封面 [草图]";
    image.text = L"封面说明";
    document.blocks.push_back(image);

    mdge::Block divider = mdge::MakeBlock(mdge::BlockType::Divider);
    document.blocks.push_back(divider);

    mdge::Block empty = mdge::MakeBlock(mdge::BlockType::Paragraph);
    empty.text = L"结尾段落。";
    document.blocks.push_back(empty);

    mdge::Block task = mdge::MakeBlock(mdge::BlockType::TaskUnchecked);
    task.text = L"发布前检查";
    document.blocks.push_back(task);
    task = mdge::MakeBlock(mdge::BlockType::TaskChecked);
    task.text = L"完成任务清单";
    document.blocks.push_back(task);
    return document;
}

void TestUtf8() {
    const std::wstring original = L"MDGE 文档 · café · 日本語 · 한글";
    std::string encoded;
    std::wstring error;
    Check(mdge::Utf8Encode(original, encoded, &error), "utf8 encode");
    std::wstring decoded;
    Check(mdge::Utf8Decode(encoded, decoded, &error), "utf8 decode");
    Check(decoded == original, "utf8 roundtrip");

    std::string invalid;
    invalid.push_back(static_cast<char>(0xC3));
    invalid.push_back(static_cast<char>(0x28));
    Check(!mdge::Utf8Decode(invalid, decoded, &error), "utf8 rejects invalid continuation");

    std::string overlong;
    overlong.push_back(static_cast<char>(0xC0));
    overlong.push_back(static_cast<char>(0xAF));
    Check(!mdge::Utf8Decode(overlong, decoded, &error), "utf8 rejects overlong");
}

void TestCrc32() {
    const std::uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    Check(mdge::Crc32(data, sizeof(data)) == 0xCBF43926u,
          "crc32 standard test vector");
}

void TestModelEditing() {
    mdge::Block block = mdge::MakeBlock(mdge::BlockType::Paragraph);
    mdge::InsertText(block, 0, L"hello world");
    mdge::ApplyInlineStyle(block, 0, 5, mdge::StyleBold, true);
    Check(mdge::StyleAt(block, 0) & mdge::StyleBold, "bold applied");
    Check(!(mdge::StyleAt(block, 6) & mdge::StyleBold), "bold range limited");

    mdge::ApplyLink(block, 6, 11, L"https://example.com");
    Check(mdge::LinkAt(block, 8) == L"https://example.com", "link applied");
    mdge::InsertText(block, 8, L"X");
    Check(mdge::LinkAt(block, 8).empty(), "typing splits link");
    Check(mdge::LinkAt(block, 11) == L"https://example.com", "link tail preserved");

    mdge::EraseText(block, 5, 7);
    Check(mdge::LinkAt(block, block.text.size() - 1) == L"https://example.com",
          "erase keeps link tail");

    mdge::Block joinLeft = mdge::MakeBlock(mdge::BlockType::Paragraph);
    mdge::InsertText(joinLeft, 0, L"hello ");
    mdge::ApplyInlineStyle(joinLeft, 0, 5, mdge::StyleBold, true);
    mdge::Block joinRight = mdge::MakeBlock(mdge::BlockType::Paragraph);
    mdge::InsertText(joinRight, 0, L"world");
    mdge::ApplyLink(joinRight, 0, 5, L"https://example.com/world");
    mdge::MergeBlock(joinLeft, joinRight);
    Check(joinLeft.text == L"hello world", "merge restores text");
    Check((mdge::StyleAt(joinLeft, 0) & mdge::StyleBold) != 0,
          "merge keeps left styles");
    Check(mdge::LinkAt(joinLeft, 8) == L"https://example.com/world",
          "merge shifts right links");
    Check(joinRight.text.empty(), "merge empties source");

    mdge::Block splitLeft = mdge::MakeBlock(mdge::BlockType::Paragraph);
    mdge::InsertText(splitLeft, 0, L"hello world");
    mdge::ApplyInlineStyle(splitLeft, 0, 6, mdge::StyleBold, true);
    mdge::Block splitRight = mdge::SplitBlock(splitLeft, 6);
    Check(splitLeft.text == L"hello ", "split trims left text");
    Check(splitRight.text == L"world", "split moves right text");
    Check((mdge::StyleAt(splitLeft, 0) & mdge::StyleBold) != 0,
          "split keeps left style");
    Check((mdge::StyleAt(splitRight, 0) & mdge::StyleBold) == 0,
          "split scopes style to left");

    mdge::Block styled = mdge::MakeBlock(mdge::BlockType::Paragraph);
    mdge::InsertText(styled, 0, L"abcdef", mdge::StyleItalic);
    mdge::ApplyInlineStyle(styled, 2, 4, mdge::StyleBold, true);
    mdge::Block tail = mdge::SplitBlock(styled, 3);
    Check(styled.text == L"abc" && tail.text == L"def", "split preserves styled text");
    Check((mdge::StyleAt(tail, 0) & mdge::StyleItalic) != 0, "split preserves styles");
}

void TestWordCounts() {
    mdge::Document document;
    mdge::Block block = mdge::MakeBlock(mdge::BlockType::Paragraph);
    block.text = L"你好 MDGE Studio 2026";
    document.blocks.push_back(block);
    Check(mdge::CountWords(document) == 5, "word count mixed CJK/Latin");
    Check(mdge::CountCharacters(document) == 19, "character count");
}

void TestMarkdown() {
    mdge::Document document = MakeRichDocument();
    const std::wstring markdown = mdge::DocumentToMarkdown(document);
    Check(Contains(markdown, L"# 让想法，轻松成文"), "markdown heading");
    Check(Contains(markdown, L"**"), "markdown bold marker");
    Check(Contains(markdown, L"*Italic"), "markdown italic marker");
    Check(Contains(markdown, L"`code`"), "markdown inline code");
    Check(Contains(markdown, L"https://example.com/"), "markdown link");
    Check(Contains(markdown, L"- 拖动左侧内容块"), "markdown bullet");
    Check(Contains(markdown, L"1. 第一步\n2. 第二步"), "markdown ordered numbering");
    mdge::Document compact;
    mdge::Block firstBullet = mdge::MakeBlock(mdge::BlockType::Bullet);
    mdge::InsertText(firstBullet, 0, L"one");
    compact.blocks.push_back(firstBullet);
    mdge::Block secondBullet = mdge::MakeBlock(mdge::BlockType::Bullet);
    mdge::InsertText(secondBullet, 0, L"two");
    compact.blocks.push_back(secondBullet);
    Check(Contains(mdge::DocumentToMarkdown(compact), L"- one\n- two"),
          "markdown compact bullet list");
    Check(Contains(markdown, L"> 少一点格式负担"), "markdown quote");
    Check(Contains(markdown, L"```cpp\n#include"), "markdown code fence language");
    Check(Contains(markdown, L"![封面 \\[草图\\]]"), "markdown image alt escape");
    Check(Contains(markdown, L"---\n"), "markdown divider");
    Check(Contains(markdown, L"- [ ] 发布前检查\n- [x] 完成任务清单"),
          "markdown task list");
    Check(mdge::IsTask(mdge::BlockType::TaskUnchecked) &&
              mdge::IsList(mdge::BlockType::TaskChecked) &&
              mdge::IsTextual(mdge::BlockType::TaskUnchecked),
          "task block helpers");
    Check(!markdown.empty() && markdown.back() == L'\n', "markdown trailing newline");

    mdge::Document reset;
    mdge::Block numbered = mdge::MakeBlock(mdge::BlockType::Numbered);
    numbered.text = L"one";
    reset.blocks.push_back(numbered);
    reset.blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
    reset.blocks.back().text = L"split";
    numbered = mdge::MakeBlock(mdge::BlockType::Numbered);
    numbered.text = L"again";
    reset.blocks.push_back(numbered);
    const std::wstring resetMd = mdge::DocumentToMarkdown(reset);
    Check(Contains(resetMd, L"1. one\n\nsplit\n\n1. again"),
          "markdown number sequence resets");

    mdge::Document fence;
    mdge::Block code = mdge::MakeBlock(mdge::BlockType::Code);
    code.text = L"```inside```";
    fence.blocks.push_back(code);
    const std::wstring fenceMd = mdge::DocumentToMarkdown(fence);
    Check(Contains(fenceMd, L"````\n```inside```\n````"), "markdown fence grows");
}

void TestMarkdownParsing() {
    const std::wstring source =
        L"# 示例标题\n\n"
        L"这是 **粗体**、*斜体*、`code` 和 [链接](https://example.com) 的组合。\n\n"
        L"- 项目一\n"
        L"- 项目二\n\n"
        L"1. 第一步\n"
        L"2. 第二步\n\n"
        L"> 引用一\n"
        L"> 引用二\n\n"
        L"```cpp\n"
        L"int value = 1;\n"
        L"```\n\n"
        L"![示例图片](assets/example.png)\n\n"
        L"---\n\n"
        L"- [ ] 待办任务\n"
        L"- [x] 已完成任务\n";
    std::vector<mdge::Block> blocks = mdge::MarkdownToBlocks(source);
    Check(blocks.size() == 12, "markdown parser block count");
    if (blocks.size() == 12) {
        Check(blocks[0].type == mdge::BlockType::Heading1 &&
                  blocks[0].text == L"示例标题",
              "markdown parser heading");
        Check(blocks[1].type == mdge::BlockType::Paragraph, "markdown parser paragraph");
        bool sawBold = false;
        bool sawItalic = false;
        bool sawCode = false;
        for (std::size_t i = 0; i < blocks[1].styles.size(); ++i) {
            sawBold = sawBold || (blocks[1].styles[i] & mdge::StyleBold) != 0;
            sawItalic = sawItalic || (blocks[1].styles[i] & mdge::StyleItalic) != 0;
            sawCode = sawCode || (blocks[1].styles[i] & mdge::StyleCode) != 0;
        }
        Check(sawBold && sawItalic && sawCode, "markdown parser inline styles");
        Check(!blocks[1].links.empty() &&
                  blocks[1].links.front().href == L"https://example.com",
              "markdown parser link");
        Check(blocks[2].type == mdge::BlockType::Bullet &&
                  blocks[2].text == L"项目一",
              "markdown parser bullet");
        Check(blocks[4].type == mdge::BlockType::Numbered &&
                  blocks[4].text == L"第一步",
              "markdown parser numbered");
        Check(blocks[6].type == mdge::BlockType::Quote &&
                  blocks[6].text == L"引用一\n引用二",
              "markdown parser quote");
        Check(blocks[7].type == mdge::BlockType::Code &&
                  blocks[7].language == L"cpp" &&
                  blocks[7].text == L"int value = 1;",
              "markdown parser fenced code");
        Check(blocks[8].type == mdge::BlockType::Image &&
                  blocks[8].imagePath == L"assets/example.png" &&
                  blocks[8].caption == L"示例图片",
              "markdown parser image");
        Check(blocks[9].type == mdge::BlockType::Divider,
              "markdown parser divider");
        Check(blocks[10].type == mdge::BlockType::TaskUnchecked &&
                  blocks[10].text == L"待办任务",
              "markdown parser unchecked task");
        Check(blocks[11].type == mdge::BlockType::TaskChecked &&
                  blocks[11].text == L"已完成任务",
              "markdown parser checked task");
    }

    mdge::Document document;
    document.blocks = blocks;
    const std::wstring firstPass = mdge::DocumentToMarkdown(document);
    mdge::Document second;
    second.blocks = mdge::MarkdownToBlocks(firstPass);
    const std::wstring secondPass = mdge::DocumentToMarkdown(second);
    Check(firstPass == secondPass, "markdown parse/export stable");

    std::vector<mdge::Block> looseTask = mdge::MarkdownToBlocks(L"- [] 未完成\n");
    Check(looseTask.size() == 1 &&
              looseTask[0].type == mdge::BlockType::TaskUnchecked &&
              looseTask[0].text == L"未完成",
          "markdown parser compact task marker");

    mdge::Document empty;
    empty.blocks = mdge::MarkdownToBlocks(L"");
    Check(empty.blocks.empty(), "markdown parser empty source");
}

void TestProjectRoundtrip(const std::wstring& directory) {
    const std::wstring path = directory + L"\\roundtrip.mdge";
    mdge::Document original = MakeRichDocument();
    mdge::NormalizeDocument(original);
    std::wstring error;
    Check(mdge::SaveProjectFile(path, original, error), "project save");
    if (!error.empty()) {
        std::printf("save error: %ls\n", error.c_str());
    }

    mdge::Document loaded;
    Check(mdge::LoadProjectFile(path, loaded, error), "project load");
    if (!error.empty()) {
        std::printf("load error: %ls\n", error.c_str());
    }
    Check(DocumentsEqual(original, loaded), "project roundtrip equality");
    Check(loaded.blocks.size() == original.blocks.size(), "project block count");

    std::vector<std::uint8_t> bytes;
    Check(mdge::ReadFileBytes(path, bytes, error), "read project bytes");
    if (bytes.size() > 40) {
        bytes[32 + 5] ^= 0x40;
        const std::wstring corruptPath = directory + L"\\corrupt.mdge";
        Check(mdge::WriteFileBytesAtomic(corruptPath, bytes, error),
              "write corrupt project");
        mdge::Document corrupt;
        std::wstring corruptError;
        Check(!mdge::LoadProjectFile(corruptPath, corrupt, corruptError),
              "project rejects bad checksum");
    }

    const std::wstring emptyPath = directory + L"\\empty.mdge";
    mdge::Document empty;
    empty.blocks.clear();
    Check(mdge::SaveProjectFile(emptyPath, empty, error), "save empty project");
    mdge::Document emptyLoaded;
    Check(mdge::LoadProjectFile(emptyPath, emptyLoaded, error), "load empty project");
    Check(emptyLoaded.title == L"未命名文档", "empty project title default");
    Check(emptyLoaded.blocks.empty(), "empty project blocks");
}

void TestPaths(const std::wstring& directory) {
    const std::wstring projectDirectory = directory + L"\\project";
    const std::wstring assetDirectory = projectDirectory + L"\\assets";
    std::wstring error;
    Check(mdge::EnsureDirectoryExists(assetDirectory, error), "create asset directory");
    const std::wstring iconPath = assetDirectory + L"\\mark.png";
    Check(mdge::WriteUtf8TextFile(iconPath, "not a real png", error),
          "create dummy icon");
    const std::wstring projectPath = projectDirectory + L"\\demo.mdge";

    mdge::ResolvedAssetPath relative =
        mdge::ResolveAssetPath(L"assets\\mark.png", projectPath);
    Check(relative.exists, "relative asset exists");
    Check(relative.kind == mdge::AssetPathKind::ProjectRelative,
          "relative asset origin");
    Check(_wcsicmp(relative.resolvedPath.c_str(), iconPath.c_str()) == 0,
          "relative asset resolved path");

    mdge::ResolvedAssetPath absolute = mdge::ResolveAssetPath(iconPath, projectPath);
    Check(absolute.exists, "absolute asset exists");
    Check(absolute.kind == mdge::AssetPathKind::Absolute,
          "absolute asset origin");

    mdge::ResolvedAssetPath missing =
        mdge::ResolveAssetPath(L"missing\\nope.png", projectPath);
    Check(!missing.exists, "missing asset reports missing");
    Check(missing.kind == mdge::AssetPathKind::Missing, "missing asset kind");

    std::wstring relativeAgain;
    Check(mdge::TryMakeRelativePath(projectDirectory, iconPath, relativeAgain),
          "make relative path");
    Check(_wcsicmp(relativeAgain.c_str(), L"assets\\mark.png") == 0,
          "relative path value");
    Check(mdge::HasImageExtension(L"a.PNG"), "image extension case insensitive");
    Check(!mdge::HasImageExtension(L"a.txt"), "non image extension");
}

void Cleanup(const std::wstring& directory) {
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
}

// 回归：Markdown 解析出的代码块带着空 styles 进入文档，而 InsertText 对
// 非文本块会提前返回。渲染端一旦用 text 的下标直接索引 styles 就是越界读取，
// 这里把"代码块里继续输入后 styles 仍然等长"固定下来。
void TestCodeBlockStyleInvariant() {
    const std::wstring source = L"```cpp\nint a = 1;\n```\n";
    const std::vector<mdge::Block> blocks = mdge::MarkdownToBlocks(source);
    Check(!blocks.empty(), "fenced code parsed");
    if (blocks.empty()) {
        return;
    }
    Check(blocks[0].type == mdge::BlockType::Code, "fenced block is code");
    Check(blocks[0].text == L"int a = 1;", "code text preserved");
    Check(blocks[0].styles.size() == blocks[0].text.size(),
          "code block styles sized to text");

    // 编辑后再规范化，styles 必须继续与 text 等长。
    mdge::Document document;
    document.blocks = blocks;
    mdge::InsertText(document.blocks[0], document.blocks[0].text.size(), L"\nint b;",
                     0);
    mdge::NormalizeDocument(document);
    Check(document.blocks[0].styles.size() == document.blocks[0].text.size(),
          "code styles stay in sync after edit");

    // 往返一遍项目文件，文本不能丢。
    std::wstring error;
    const std::wstring path = mdge::CurrentDirectoryPath() +
                              L"\\build\\mdge-tests-codeb" +
                              std::to_wstring(GetCurrentProcessId()) + L".mdge";
    Check(mdge::SaveProjectFile(path, document, error), "code save");
    mdge::Document reloaded;
    Check(mdge::LoadProjectFile(path, reloaded, error), "code load");
    Check(!reloaded.blocks.empty() &&
              reloaded.blocks[0].text == document.blocks[0].text,
          "code text roundtrips");
    Check(!reloaded.blocks.empty() &&
              reloaded.blocks[0].styles.size() ==
                  reloaded.blocks[0].text.size(),
          "code styles sized after load");
    DeleteFileW(path.c_str());
}

// 回归：未闭合的围栏如果一路吞到文件末尾，会把后续所有内容静默变成代码。
void TestUnterminatedFence() {
    const std::wstring source = L"```cpp\nint a = 1;\n\n# 后面的标题\n\n正文。\n";
    const std::vector<mdge::Block> blocks = mdge::MarkdownToBlocks(source);

    bool sawHeading = false;
    bool sawParagraph = false;
    for (const mdge::Block& block : blocks) {
        if (block.type == mdge::BlockType::Heading1) {
            sawHeading = true;
        }
        if (block.type == mdge::BlockType::Paragraph) {
            sawParagraph = true;
        }
    }
    Check(sawHeading, "unterminated fence keeps following heading");
    Check(sawParagraph, "unterminated fence keeps following paragraph");

    // 完全闭合的围栏仍然必须解析成代码块。
    const std::vector<mdge::Block> closed =
        mdge::MarkdownToBlocks(L"```cpp\nint a = 1;\n```\n");
    Check(!closed.empty() && closed[0].type == mdge::BlockType::Code,
          "closed fence still parses as code");
}

// 回归：图片说明曾经用 SerializeInline 输出并再套一层 `*`，带粗体或链接的
// 说明会形成嵌套强调，导出后重新解析会多出一个段落块。
void TestImageCaptionRoundtrip() {
    mdge::Block image = mdge::MakeBlock(mdge::BlockType::Image);
    image.imagePath = L"a.png";
    image.caption = L"说明文字";
    image.text = L"带样式**说明**";
    image.styles.assign(image.text.size(), 0);
    mdge::ApplyInlineStyle(image, 2, 4, mdge::StyleBold, true);

    mdge::Document document;
    document.title = L"t";
    document.blocks.push_back(image);
    const std::wstring exported = mdge::DocumentToMarkdown(document);
    Check(Contains(exported, L"![说明文字](a.png)"), "image alt exported");

    const std::vector<mdge::Block> reparsed = mdge::MarkdownToBlocks(exported);
    int imageBlocks = 0;
    for (const mdge::Block& block : reparsed) {
        if (block.type == mdge::BlockType::Image) {
            ++imageBlocks;
        }
    }
    Check(imageBlocks == 1, "image caption does not split into extra blocks");
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    const std::wstring directory = mdge::CurrentDirectoryPath() + L"\\build\\mdge-tests-" +
                                   std::to_wstring(GetCurrentProcessId());
    std::wstring error;
    if (!mdge::EnsureDirectoryExists(directory, error)) {
        std::printf("Unable to create the test directory.\n");
        std::printf("path: %s\n", mdge::Utf8Encode(directory).c_str());
        std::printf("error: %s\n", mdge::Utf8Encode(error).c_str());
        return 2;
    }

    std::printf("MDGE core tests\n");
    TestUtf8();
    TestCrc32();
    TestModelEditing();
    TestWordCounts();
    TestMarkdown();
    TestMarkdownParsing();
    TestCodeBlockStyleInvariant();
    TestUnterminatedFence();
    TestImageCaptionRoundtrip();
    TestProjectRoundtrip(directory);
    TestPaths(directory);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    Cleanup(directory);
    return g_failures == 0 ? 0 : 1;
}
