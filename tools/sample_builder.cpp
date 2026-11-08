#include "../src/core/file_utils.h"
#include "../src/core/markdown.h"
#include "../src/core/model.h"
#include "../src/core/project_io.h"
#include "../src/core/utf8.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace {

using mdge::Block;
using mdge::BlockType;

std::uint64_t NowSeconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

Block TextBlock(BlockType type, const std::wstring& text) {
    Block block = mdge::MakeBlock(type);
    mdge::InsertText(block, 0, text);
    return block;
}

}  // namespace

int main() {
    mdge::Document document;
    document.title = L"把想法，写成一篇好文档";
    document.author = L"MDGE Studio";
    document.iconPath = L"assets/mdge-mark.png";
    document.created = NowSeconds();
    document.modified = document.created;

    Block title = mdge::MakeBlock(BlockType::Heading1);
    mdge::InsertText(title, 0, L"视觉写作，从此简单");
    document.blocks.push_back(std::move(title));

    Block intro = mdge::MakeBlock(BlockType::Paragraph);
    mdge::InsertText(intro, 0, L"MDGE Studio 把 Markdown 的秩序感，变成可以直接书写与拖动的画布。");
    mdge::InsertText(intro, intro.text.size(), L" 你不需要记住语法");
    mdge::ApplyInlineStyle(intro, intro.text.size() - 7, intro.text.size(),
                           mdge::StyleBold, true);
    mdge::InsertText(intro, intro.text.size(), L"，只需要专注于表达。");
    document.blocks.push_back(std::move(intro));

    Block section = mdge::MakeBlock(BlockType::Heading2);
    mdge::InsertText(section, 0, L"拖一拖，结构就出现了");
    document.blocks.push_back(std::move(section));

    Block hint = mdge::MakeBlock(BlockType::Paragraph);
    mdge::InsertText(hint, 0, L"在左侧选择“项目列表”或“编号列表”，再拖动排序。想了解 Markdown，可以访问 ");
    const std::size_t linkStart = hint.text.size();
    mdge::InsertText(hint, hint.text.size(), L"CommonMark 规范");
    mdge::ApplyLink(hint, linkStart, hint.text.size(),
                    L"https://commonmark.org/");
    mdge::InsertText(hint, hint.text.size(), L"。");
    document.blocks.push_back(std::move(hint));

    document.blocks.push_back(TextBlock(
        BlockType::Bullet, L"从左侧拖入内容块，放到画布中的任意位置"));
    document.blocks.push_back(
        TextBlock(BlockType::Bullet, L"拖动行首把手，调整段落顺序"));
    document.blocks.push_back(
        TextBlock(BlockType::Bullet, L"把图片文件直接拖进画布，即刻完成插图"));
    document.blocks.push_back(
        TextBlock(BlockType::Numbered, L"在画布中直接输入文字，Enter 拆分段落"));
    document.blocks.push_back(
        TextBlock(BlockType::Numbered, L"按 Ctrl+S 保存项目，右上角可以导出 Markdown"));

    Block quote = mdge::MakeBlock(BlockType::Quote);
    mdge::InsertText(quote, 0, L"少一点格式负担，多一点表达本身。");
    document.blocks.push_back(std::move(quote));

    Block codeTitle = mdge::MakeBlock(BlockType::Heading2);
    mdge::InsertText(codeTitle, 0, L"只用描述，就能生成 Markdown");
    document.blocks.push_back(std::move(codeTitle));

    Block codeIntro = mdge::MakeBlock(BlockType::Paragraph);
    mdge::InsertText(codeIntro, 0, L"导出的文件是标准 ");
    const std::size_t inlineCodeStart = codeIntro.text.size();
    mdge::InsertText(codeIntro, codeIntro.text.size(), L".md", mdge::StyleCode);
    mdge::InsertText(codeIntro, codeIntro.text.size(),
                     L" 文档，而项目本身保存在自有的 ");
    const std::size_t containerStart = codeIntro.text.size();
    mdge::InsertText(codeIntro, codeIntro.text.size(), L".mdge",
                     mdge::StyleCode);
    mdge::InsertText(codeIntro, codeIntro.text.size(), L" 容器中。");
    (void)inlineCodeStart;
    (void)containerStart;
    document.blocks.push_back(std::move(codeIntro));

    Block code = mdge::MakeBlock(BlockType::Code);
    mdge::InsertText(code, 0,
                     L"mdge::Document doc = mdge::MakeStarterDocument();\n"
                     L"doc.title = L\"我的下一篇文档\";\n\n"
                     L"if (!mdge::SaveProjectFile(L\"story.mdge\", doc, error)) {\n"
                     L"    return 1;\n"
                     L"}");
    code.language = L"cpp";
    document.blocks.push_back(std::move(code));

    Block imageTitle = mdge::MakeBlock(BlockType::Heading3);
    mdge::InsertText(imageTitle, 0, L"项目图标支持绝对路径与相对路径");
    document.blocks.push_back(std::move(imageTitle));

    Block image = mdge::MakeBlock(BlockType::Image);
    image.imagePath = L"assets/mdge-mark.png";
    image.caption = L"当前项目使用相对路径载入图标";
    document.blocks.push_back(std::move(image));

    document.blocks.push_back(mdge::MakeBlock(BlockType::Divider));

    Block finish = mdge::MakeBlock(BlockType::Heading3);
    mdge::InsertText(finish, 0, L"准备好开始了吗");
    document.blocks.push_back(std::move(finish));

    Block outro = mdge::MakeBlock(BlockType::Paragraph);
    mdge::InsertText(outro, 0,
                     L"按 Ctrl+N 新建项目，或直接修改这篇示例。所有操作都可以撤销，记得在离开前保存。");
    document.blocks.push_back(std::move(outro));

    std::wstring error;
    if (!mdge::SaveProjectFile(L"samples\\Welcome.mdge", document, error)) {
        std::printf("save failed: %s\n", mdge::Utf8Encode(error).c_str());
        return 1;
    }
    const std::wstring markdown = mdge::DocumentToMarkdown(document);
    if (!mdge::WriteUtf8TextFile(L"samples\\Welcome.md", mdge::Utf8Encode(markdown),
                                 error)) {
        std::printf("markdown failed: %s\n", mdge::Utf8Encode(error).c_str());
        return 1;
    }
    std::printf("sample written\n");
    return 0;
}
