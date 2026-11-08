#include "project_io.h"

#include "file_utils.h"
#include "utf8.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace mdge {
namespace {

constexpr std::uint8_t kTagTitle = 0x01;
constexpr std::uint8_t kTagAuthor = 0x02;
constexpr std::uint8_t kTagIcon = 0x03;
constexpr std::uint8_t kTagCreated = 0x05;
constexpr std::uint8_t kTagModified = 0x06;
constexpr std::uint8_t kTagApp = 0x07;
constexpr std::uint8_t kTagBlock = 0x10;
constexpr std::uint8_t kTagEnd = 0x7F;

constexpr std::size_t kHeaderSize = 32;
constexpr char kMagic[4] = {'M', 'D', 'G', 'E'};
constexpr char kFooter[4] = {'E', 'N', 'D', 'G'};
constexpr std::uint32_t kHeaderFlagUtf8 = 1u << 0;
constexpr std::uint32_t kMaxRecordCount = 200000;
constexpr std::uint32_t kMaxSpanCount = 2000000;

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void PutU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void PutUtf8String(std::vector<std::uint8_t>& out, const std::wstring& text,
                   std::wstring& error) {
    std::string encoded;
    if (!Utf8Encode(text, encoded, &error)) {
        return;
    }
    if (encoded.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = L"单个文本字段过大。";
        return;
    }
    PutU32(out, static_cast<std::uint32_t>(encoded.size()));
    out.insert(out.end(), encoded.begin(), encoded.end());
}

void AppendRecord(std::vector<std::uint8_t>& body, std::uint8_t tag,
                  const std::vector<std::uint8_t>& payload) {
    body.push_back(tag);
    body.push_back(0);
    PutU16(body, 1);
    PutU32(body, static_cast<std::uint32_t>(payload.size()));
    body.insert(body.end(), payload.begin(), payload.end());
}

void AppendStringRecord(std::vector<std::uint8_t>& body, std::uint8_t tag,
                        const std::wstring& value, std::wstring& error) {
    std::vector<std::uint8_t> payload;
    PutUtf8String(payload, value, error);
    if (!error.empty()) {
        return;
    }
    AppendRecord(body, tag, payload);
}

void AppendTimestampRecord(std::vector<std::uint8_t>& body, std::uint8_t tag,
                           std::uint64_t value) {
    std::vector<std::uint8_t> payload;
    PutU64(payload, value);
    AppendRecord(body, tag, payload);
}

std::vector<std::uint8_t> BuildBlockPayload(const Block& block,
                                            std::wstring& error) {
    std::vector<std::uint8_t> payload;
    payload.push_back(static_cast<std::uint8_t>(block.type));
    payload.push_back(0);
    PutU16(payload, 0);
    PutU32(payload, 0);  // block flags, reserved for future use
    PutUtf8String(payload, block.text, error);
    if (!error.empty()) {
        return {};
    }
    PutUtf8String(payload, block.imagePath, error);
    if (!error.empty()) {
        return {};
    }
    PutUtf8String(payload, block.caption, error);
    if (!error.empty()) {
        return {};
    }
    PutUtf8String(payload, block.language, error);
    if (!error.empty()) {
        return {};
    }

    struct Span {
        std::uint32_t start = 0;
        std::uint32_t length = 0;
        std::uint8_t flags = 0;
        std::wstring href;
    };
    std::vector<Span> spans;
    std::size_t index = 0;
    while (index < block.text.size()) {
        std::uint8_t flags =
            index < block.styles.size()
                ? static_cast<std::uint8_t>(block.styles[index] &
                                            (StyleBold | StyleItalic | StyleCode |
                                             StyleLink))
                : 0;
        std::wstring href;
        if ((flags & StyleLink) != 0) {
            href = LinkAt(block, index);
            if (href.empty()) {
                flags = static_cast<std::uint8_t>(flags & ~StyleLink);
            }
        }
        if (flags == 0) {
            ++index;
            continue;
        }
        const std::size_t start = index;
        ++index;
        while (index < block.text.size()) {
            std::uint8_t nextFlags =
                index < block.styles.size()
                    ? static_cast<std::uint8_t>(block.styles[index] &
                                                (StyleBold | StyleItalic |
                                                 StyleCode | StyleLink))
                    : 0;
            std::wstring nextHref;
            if ((nextFlags & StyleLink) != 0) {
                nextHref = LinkAt(block, index);
                if (nextHref.empty()) {
                    nextFlags = static_cast<std::uint8_t>(nextFlags & ~StyleLink);
                }
            }
            if (nextFlags != flags || nextHref != href) {
                break;
            }
            ++index;
        }
        Span span;
        span.start = static_cast<std::uint32_t>(start);
        span.length = static_cast<std::uint32_t>(index - start);
        span.flags = flags;
        span.href = std::move(href);
        spans.push_back(std::move(span));
    }

    if (spans.size() > kMaxSpanCount) {
        error = L"文档包含过多的内联样式区间。";
        return {};
    }
    PutU32(payload, static_cast<std::uint32_t>(spans.size()));
    for (const Span& span : spans) {
        PutU32(payload, span.start);
        PutU32(payload, span.length);
        payload.push_back(span.flags);
        payload.push_back(0);
        payload.push_back(0);
        payload.push_back(0);
        PutUtf8String(payload, span.href, error);
        if (!error.empty()) {
            return {};
        }
    }
    return payload;
}

class Cursor {
  public:
    Cursor(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool U8(std::uint8_t& value) {
        if (remaining() < 1) {
            return false;
        }
        value = data_[offset_++];
        return true;
    }

    bool U16(std::uint16_t& value) {
        if (remaining() < 2) {
            return false;
        }
        value = static_cast<std::uint16_t>(data_[offset_]) |
                (static_cast<std::uint16_t>(data_[offset_ + 1]) << 8);
        offset_ += 2;
        return true;
    }

    bool U32(std::uint32_t& value) {
        if (remaining() < 4) {
            return false;
        }
        value = static_cast<std::uint32_t>(data_[offset_]) |
                (static_cast<std::uint32_t>(data_[offset_ + 1]) << 8) |
                (static_cast<std::uint32_t>(data_[offset_ + 2]) << 16) |
                (static_cast<std::uint32_t>(data_[offset_ + 3]) << 24);
        offset_ += 4;
        return true;
    }

    bool U64(std::uint64_t& value) {
        if (remaining() < 8) {
            return false;
        }
        value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(data_[offset_ + static_cast<std::size_t>(i)])
                     << (8 * i);
        }
        offset_ += 8;
        return true;
    }

    bool String(std::wstring& value, std::wstring& error) {
        std::uint32_t length = 0;
        if (!U32(length)) {
            error = L"项目文件中的字符串长度字段不完整。";
            return false;
        }
        if (length > remaining()) {
            error = L"项目文件中的字符串超出记录边界。";
            return false;
        }
        const char* chars = reinterpret_cast<const char*>(data_ + offset_);
        if (!Utf8Decode(std::string_view(chars, length), value, &error)) {
            return false;
        }
        offset_ += length;
        return true;
    }

    bool Skip(std::size_t count) {
        if (count > remaining()) {
            return false;
        }
        offset_ += count;
        return true;
    }

    std::size_t remaining() const { return size_ - offset_; }
    std::size_t offset() const { return offset_; }

  private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

bool ParseBlock(const std::uint8_t* data, std::size_t size, Block& block,
                std::wstring& error) {
    Cursor cursor(data, size);
    std::uint8_t type = 0;
    std::uint8_t reserved8 = 0;
    std::uint16_t reserved16 = 0;
    std::uint32_t blockFlags = 0;
    if (!cursor.U8(type) || !cursor.U8(reserved8) || !cursor.U16(reserved16) ||
        !cursor.U32(blockFlags)) {
        error = L"内容块头部不完整。";
        return false;
    }
    (void)reserved8;
    (void)reserved16;
    (void)blockFlags;
    if (type > static_cast<std::uint8_t>(BlockType::TaskChecked)) {
        error = L"项目文件包含当前版本不支持的内容块类型。";
        return false;
    }
    block = Block{};
    block.type = static_cast<BlockType>(type);

    if (!cursor.String(block.text, error) || !cursor.String(block.imagePath, error) ||
        !cursor.String(block.caption, error) || !cursor.String(block.language, error)) {
        return false;
    }

    std::uint32_t spanCount = 0;
    if (!cursor.U32(spanCount)) {
        error = L"内容块缺少内联样式数量。";
        return false;
    }
    if (spanCount > kMaxSpanCount) {
        error = L"内容块包含过多的内联样式区间。";
        return false;
    }

    block.styles.assign(block.text.size(), 0);
    for (std::uint32_t i = 0; i < spanCount; ++i) {
        std::uint32_t start = 0;
        std::uint32_t length = 0;
        std::uint8_t flags = 0;
        std::uint8_t pad[3]{};
        if (!cursor.U32(start) || !cursor.U32(length) || !cursor.U8(flags) ||
            !cursor.U8(pad[0]) || !cursor.U8(pad[1]) || !cursor.U8(pad[2])) {
            error = L"内联样式记录不完整。";
            return false;
        }
        std::wstring href;
        if (!cursor.String(href, error)) {
            return false;
        }
        if (flags & ~(StyleBold | StyleItalic | StyleCode | StyleLink)) {
            error = L"内联样式包含未知标志。";
            return false;
        }
        if (start > block.text.size() ||
            length > block.text.size() - start) {
            error = L"内联样式范围超出文本长度。";
            return false;
        }
        if (flags & StyleLink) {
            if (href.empty()) {
                flags = static_cast<std::uint8_t>(flags & ~StyleLink);
            } else {
                block.links.push_back(
                    LinkRun{static_cast<std::size_t>(start),
                            static_cast<std::size_t>(length), href});
            }
        }
        for (std::uint32_t j = 0; j < length; ++j) {
            const std::size_t at = static_cast<std::size_t>(start + j);
            block.styles[at] = static_cast<std::uint8_t>(block.styles[at] | flags);
        }
    }
    NormalizeBlock(block);
    // The in-memory revision starts at a fresh number for each loaded block.
    block.revision = 1;
    if (cursor.remaining() != 0) {
        // Forward-compatible trailing fields are allowed inside a block record.
    }
    return true;
}

}  // namespace

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(-(static_cast<int>(crc & 1u)));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool SaveProjectFile(const std::wstring& path, const Document& document,
                     std::wstring& error) {
    error.clear();
    if (path.empty()) {
        error = L"尚未指定项目文件路径。";
        return false;
    }

    std::vector<std::uint8_t> body;
    AppendStringRecord(body, kTagTitle, document.title, error);
    if (!error.empty()) {
        return false;
    }
    AppendStringRecord(body, kTagAuthor, document.author, error);
    if (!error.empty()) {
        return false;
    }
    AppendStringRecord(body, kTagIcon, document.iconPath, error);
    if (!error.empty()) {
        return false;
    }
    AppendTimestampRecord(body, kTagCreated, document.created);
    AppendTimestampRecord(body, kTagModified, document.modified);
    AppendStringRecord(body, kTagApp, L"MDGE Studio", error);
    if (!error.empty()) {
        return false;
    }

    if (document.blocks.size() > kMaxRecordCount) {
        error = L"文档内容块数量过多。";
        return false;
    }
    for (const Block& block : document.blocks) {
        std::vector<std::uint8_t> payload = BuildBlockPayload(block, error);
        if (!error.empty()) {
            return false;
        }
        AppendRecord(body, kTagBlock, payload);
    }
    AppendRecord(body, kTagEnd, {});

    if (body.size() > kProjectMaxFileBytes) {
        error = L"项目文件超过 64 MB 大小限制。";
        return false;
    }

    std::vector<std::uint8_t> file;
    file.reserve(kHeaderSize + body.size() + 4);
    file.insert(file.end(), kMagic, kMagic + 4);
    PutU16(file, kProjectFormatVersionMajor);
    PutU16(file, kProjectFormatVersionMinor);
    PutU32(file, static_cast<std::uint32_t>(kHeaderSize));
    PutU32(file, kHeaderFlagUtf8);
    PutU64(file, static_cast<std::uint64_t>(body.size()));
    PutU32(file, Crc32(body.data(), body.size()));
    PutU32(file, 0);
    file.insert(file.end(), body.begin(), body.end());
    file.insert(file.end(), kFooter, kFooter + 4);

    if (!WriteFileBytesAtomic(path, file, error)) {
        return false;
    }
    return true;
}

bool LoadProjectFile(const std::wstring& path, Document& document,
                     std::wstring& error) {
    error.clear();
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, error, kProjectMaxFileBytes + 4096)) {
        return false;
    }
    if (bytes.size() < kHeaderSize + 4) {
        error = L"这不是有效的 MDGE 项目文件（文件过短）。";
        return false;
    }
    if (std::memcmp(bytes.data(), kMagic, 4) != 0) {
        error = L"这不是有效的 MDGE 项目文件（签名错误）。";
        return false;
    }

    Cursor header(bytes.data() + 4, kHeaderSize - 4);
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint32_t headerSize = 0;
    std::uint32_t flags = 0;
    std::uint64_t bodySize = 0;
    std::uint32_t checksum = 0;
    std::uint32_t reserved = 0;
    if (!header.U16(major) || !header.U16(minor) || !header.U32(headerSize) ||
        !header.U32(flags) || !header.U64(bodySize) || !header.U32(checksum) ||
        !header.U32(reserved)) {
        error = L"项目文件头部不完整。";
        return false;
    }
    (void)reserved;
    if (major != kProjectFormatVersionMajor) {
        error = L"项目文件版本不受支持（版本 " + std::to_wstring(major) + L"." +
                std::to_wstring(minor) + L"）。";
        return false;
    }
    if (headerSize != kHeaderSize) {
        error = L"项目文件头部大小无效。";
        return false;
    }
    if ((flags & ~kHeaderFlagUtf8) != 0) {
        error = L"项目文件包含未知的全局标志。";
        return false;
    }
    if (bodySize > kProjectMaxFileBytes) {
        error = L"项目文件内容超过大小限制。";
        return false;
    }
    if (bytes.size() != kHeaderSize + bodySize + 4) {
        error = L"项目文件大小与头部声明不一致。";
        return false;
    }
    if (std::memcmp(bytes.data() + bytes.size() - 4, kFooter, 4) != 0) {
        error = L"项目文件尾标志缺失，文件可能已损坏。";
        return false;
    }
    const std::uint8_t* body = bytes.data() + kHeaderSize;
    if (Crc32(body, static_cast<std::size_t>(bodySize)) != checksum) {
        error = L"项目文件校验失败，文件可能已损坏。";
        return false;
    }

    Document loaded;
    loaded.title.clear();
    bool sawTitle = false;
    bool sawEnd = false;
    std::uint32_t recordCount = 0;
    Cursor cursor(body, static_cast<std::size_t>(bodySize));

    while (cursor.remaining() > 0) {
        if (++recordCount > kMaxRecordCount) {
            error = L"项目文件记录数量异常。";
            return false;
        }
        std::uint8_t tag = 0;
        std::uint8_t reserved8 = 0;
        std::uint16_t version = 0;
        std::uint32_t payloadSize = 0;
        if (!cursor.U8(tag) || !cursor.U8(reserved8) || !cursor.U16(version) ||
            !cursor.U32(payloadSize)) {
            error = L"项目文件记录头部不完整。";
            return false;
        }
        (void)reserved8;
        (void)version;
        if (payloadSize > cursor.remaining()) {
            error = L"项目文件记录长度超出文件边界。";
            return false;
        }
        Cursor payloadCursor(body + cursor.offset(), payloadSize);
        const std::uint8_t* payload = body + cursor.offset();
        if (!cursor.Skip(payloadSize)) {
            error = L"项目文件记录被截断。";
            return false;
        }

        switch (tag) {
            case kTagTitle:
            case kTagAuthor:
            case kTagIcon:
            case kTagApp: {
                std::wstring value;
                if (!payloadCursor.String(value, error)) {
                    return false;
                }
                if (tag == kTagTitle) {
                    loaded.title = std::move(value);
                    sawTitle = true;
                } else if (tag == kTagAuthor) {
                    loaded.author = std::move(value);
                } else if (tag == kTagIcon) {
                    loaded.iconPath = std::move(value);
                }
                break;
            }
            case kTagCreated:
            case kTagModified: {
                std::uint64_t timestamp = 0;
                if (!payloadCursor.U64(timestamp)) {
                    error = L"项目时间戳记录不完整。";
                    return false;
                }
                if (tag == kTagCreated) {
                    loaded.created = timestamp;
                } else {
                    loaded.modified = timestamp;
                }
                break;
            }
            case kTagBlock: {
                Block block;
                if (!ParseBlock(payload, payloadSize, block, error)) {
                    return false;
                }
                loaded.blocks.push_back(std::move(block));
                break;
            }
            case kTagEnd:
                sawEnd = true;
                break;
            default:
                break;
        }
        if (sawEnd) {
            if (cursor.remaining() != 0) {
                error = L"项目结束标记后存在多余数据。";
                return false;
            }
            break;
        }
    }

    if (!sawEnd) {
        error = L"项目文件缺少结束标记，文件可能已损坏。";
        return false;
    }
    if (!sawTitle || loaded.title.empty()) {
        loaded.title = L"未命名文档";
    }
    NormalizeDocument(loaded);
    document = std::move(loaded);
    return true;
}

}  // namespace mdge
