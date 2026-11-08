#include "../src/core/file_utils.h"
#include "../src/core/markdown.h"
#include "../src/core/model.h"
#include "../src/core/project_io.h"
#include "../src/core/utf8.h"

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: project_dump <file.mdge>\n");
        return 2;
    }
    mdge::Document document;
    std::wstring error;
    if (!mdge::LoadProjectFile(argv[1], document, error)) {
        std::printf("load failed: %s\n", mdge::Utf8Encode(error).c_str());
        return 1;
    }
    std::printf("title: %s\n", mdge::Utf8Encode(document.title).c_str());
    std::printf("author: %s\n", mdge::Utf8Encode(document.author).c_str());
    std::printf("icon: %s\n", mdge::Utf8Encode(document.iconPath).c_str());
    std::printf("blocks: %zu\n", document.blocks.size());
    for (std::size_t i = 0; i < document.blocks.size(); ++i) {
        const mdge::Block& block = document.blocks[i];
        std::printf("--- block %zu type=%d ---\n", i,
                    static_cast<int>(block.type));
        std::printf("text: %s\n", mdge::Utf8Encode(block.text).c_str());
        for (std::size_t p = 0; p < block.text.size();) {
            const std::uint8_t style = block.styles[p];
            std::size_t q = p + 1;
            while (q < block.text.size() && block.styles[q] == style) {
                ++q;
            }
            if (style != 0) {
                const std::wstring fragment = block.text.substr(p, q - p);
                std::printf("style %u chars %zu-%zu: %s\n",
                            static_cast<unsigned>(style), p, q,
                            mdge::Utf8Encode(fragment).c_str());
            }
            p = q;
        }
        for (const auto& link : block.links) {
            std::printf("link %zu-%zu: %s\n", link.start, link.end(),
                        mdge::Utf8Encode(link.href).c_str());
        }
    }
    std::printf("--- markdown ---\n%s", mdge::Utf8Encode(mdge::DocumentToMarkdown(document)).c_str());
    return 0;
}
