// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Media.h"

#include <cctype>
#include <cstdint>
#include <cstring>

namespace Glim::Media {

namespace {

bool startsWith(std::string_view bytes, std::string_view prefix)
{
    return bytes.size() >= prefix.size() && bytes.substr(0, prefix.size()) == prefix;
}

std::uint32_t be32(std::string_view b, std::size_t at)
{
    return (std::uint32_t(std::uint8_t(b[at])) << 24) | (std::uint32_t(std::uint8_t(b[at + 1])) << 16)
        | (std::uint32_t(std::uint8_t(b[at + 2])) << 8) | std::uint32_t(std::uint8_t(b[at + 3]));
}

std::uint32_t le32(std::string_view b, std::size_t at)
{
    return std::uint32_t(std::uint8_t(b[at])) | (std::uint32_t(std::uint8_t(b[at + 1])) << 8)
        | (std::uint32_t(std::uint8_t(b[at + 2])) << 16) | (std::uint32_t(std::uint8_t(b[at + 3])) << 24);
}

std::uint64_t be64(std::string_view b, std::size_t at)
{
    return (std::uint64_t(be32(b, at)) << 32) | be32(b, at + 4);
}

void putLe32(std::string& out, std::size_t at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out[at + std::size_t(i)] = char((value >> (8 * i)) & 0xFF);
}

// ── JPEG ──────────────────────────────────────────────────────────────────

/// The EXIF orientation (1 to 8) in an APP1 segment's payload, 0 when none.
int exifOrientation(std::string_view app1)
{
    if (!startsWith(app1, std::string_view("Exif\0\0", 6)) || app1.size() < 6 + 8)
        return 0;
    const std::string_view tiff = app1.substr(6);
    const bool little = startsWith(tiff, "II");
    if (!little && !startsWith(tiff, "MM"))
        return 0;
    const auto u16 = [&](std::size_t at) -> unsigned {
        if (at + 2 > tiff.size())
            return 0;
        return little ? unsigned(std::uint8_t(tiff[at])) | (unsigned(std::uint8_t(tiff[at + 1])) << 8)
                      : (unsigned(std::uint8_t(tiff[at])) << 8) | unsigned(std::uint8_t(tiff[at + 1]));
    };
    const std::size_t ifd = little ? le32(tiff, 4) : be32(tiff, 4);
    if (ifd + 2 > tiff.size())
        return 0;
    const unsigned entries = u16(ifd);
    for (unsigned i = 0; i < entries; ++i) {
        const std::size_t entry = ifd + 2 + 12 * std::size_t(i);
        if (entry + 12 > tiff.size())
            return 0;
        if (u16(entry) == 0x0112) {
            const unsigned value = u16(entry + 8);
            return value >= 1 && value <= 8 ? int(value) : 0;
        }
    }
    return 0;
}

/// An APP1 segment of EXIF with the orientation alone.
std::string orientationSegment(int orientation)
{
    // "Exif\0\0", a big-endian TIFF header, IFD0 of one SHORT entry, no next IFD.
    const unsigned char payload[] = {'E', 'x', 'i', 'f',  0,    0, 'M', 'M', 0, 42, 0, 0, 0,
                                     8,   0,   1,   0x01, 0x12, 0, 3,   0,   0, 0,  1, 0, std::uint8_t(orientation),
                                     0,   0,   0,   0,    0,    0};
    const std::size_t length = sizeof payload + 2;
    std::string out = {char(0xFF), char(0xE1), char(length >> 8), char(length & 0xFF)};
    out.append(reinterpret_cast<const char*>(payload), sizeof payload);
    return out;
}

std::optional<std::string> stripJpeg(std::string_view b)
{
    if (!startsWith(b, "\xFF\xD8"))
        return std::nullopt;
    std::string out = "\xFF\xD8";
    int orientation = 0;
    bool orientationWritten = false;
    std::size_t at = 2;
    while (true) {
        if (at + 4 > b.size() || std::uint8_t(b[at]) != 0xFF)
            return std::nullopt;
        const std::uint8_t marker = std::uint8_t(b[at + 1]);
        if (marker == 0xFF) { // fill byte
            ++at;
            continue;
        }
        const std::size_t length = (std::size_t(std::uint8_t(b[at + 2])) << 8) | std::uint8_t(b[at + 3]);
        if (length < 2 || at + 2 + length > b.size())
            return std::nullopt;
        const std::string_view segment = b.substr(at, 2 + length);
        const std::string_view payload = segment.substr(4);
        const bool app = marker >= 0xE0 && marker <= 0xEF;
        const bool keep = !app || marker == 0xE0 || marker == 0xE2 || marker == 0xEE;
        if (marker == 0xE1 && !orientation)
            orientation = exifOrientation(payload);
        if (marker == 0xDA) { // the scan: the orientation goes before it, then the data to the image's end
            if (orientation > 1 && !orientationWritten)
                out += orientationSegment(orientation);
            // In the entropy-coded data an FF is followed by 00 or a restart
            // marker; FF D9 is the image's end, and what follows is no image.
            std::size_t end = at + 2 + length;
            while (end + 1 < b.size() && !(std::uint8_t(b[end]) == 0xFF && std::uint8_t(b[end + 1]) == 0xD9))
                ++end;
            if (end + 1 >= b.size())
                return std::nullopt;
            out.append(b.substr(at, end + 2 - at));
            return out;
        }
        if (keep && marker != 0xFE) {
            // The orientation goes right after JFIF's APP0, or first.
            if (marker != 0xE0 && orientation > 1 && !orientationWritten) {
                out += orientationSegment(orientation);
                orientationWritten = true;
            }
            out.append(segment);
        }
        at += 2 + length;
    }
}

// ── PNG ───────────────────────────────────────────────────────────────────

std::optional<std::string> stripPng(std::string_view b)
{
    static constexpr std::string_view Signature("\x89PNG\r\n\x1a\n", 8);
    if (!startsWith(b, Signature))
        return std::nullopt;
    std::string out(Signature);
    std::size_t at = Signature.size();
    while (at + 12 <= b.size()) {
        const std::size_t length = be32(b, at);
        if (length > b.size() || at + 12 + length > b.size())
            return std::nullopt;
        const std::string_view type = b.substr(at + 4, 4);
        const bool drop = type == "tEXt" || type == "zTXt" || type == "iTXt" || type == "eXIf" || type == "tIME";
        if (!drop)
            out.append(b.substr(at, 12 + length));
        at += 12 + length;
        if (type == "IEND")
            return out;
    }
    return std::nullopt;
}

// ── WebP ──────────────────────────────────────────────────────────────────

std::optional<std::string> stripWebp(std::string_view b)
{
    if (b.size() < 12 || !startsWith(b, "RIFF") || b.substr(8, 4) != "WEBP")
        return std::nullopt;
    std::string out(b.substr(0, 12));
    std::size_t at = 12;
    std::size_t vp8x = std::string::npos;
    const std::size_t end = std::min<std::size_t>(b.size(), std::size_t(le32(b, 4)) + 8);
    while (at + 8 <= end) {
        const std::size_t size = le32(b, at + 4);
        const std::size_t padded = size + (size & 1);
        if (at + 8 + size > end)
            return std::nullopt;
        const std::string_view fourcc = b.substr(at, 4);
        if (fourcc != "EXIF" && fourcc != "XMP ") {
            if (fourcc == "VP8X")
                vp8x = out.size();
            out.append(b.substr(at, std::min(8 + padded, end - at)));
        }
        at += 8 + padded;
    }
    if (vp8x != std::string::npos && vp8x + 8 < out.size())
        out[vp8x + 8] = char(std::uint8_t(out[vp8x + 8]) & ~0x0C); // the EXIF and XMP flags
    putLe32(out, 4, std::uint32_t(out.size() - 8));
    return out;
}

// ── GIF ───────────────────────────────────────────────────────────────────

/// The end of the data sub-blocks that start at @p at, or npos.
std::size_t skipSubBlocks(std::string_view b, std::size_t at)
{
    while (at < b.size()) {
        const std::size_t size = std::uint8_t(b[at]);
        ++at;
        if (size == 0)
            return at;
        at += size;
    }
    return std::string_view::npos;
}

std::optional<std::string> stripGif(std::string_view b)
{
    if (b.size() < 13 || (!startsWith(b, "GIF87a") && !startsWith(b, "GIF89a")))
        return std::nullopt;
    const std::uint8_t flags = std::uint8_t(b[10]);
    std::size_t at = 13 + ((flags & 0x80) ? 3 * (std::size_t(1) << ((flags & 7) + 1)) : 0);
    if (at > b.size())
        return std::nullopt;
    std::string out(b.substr(0, at));
    while (at < b.size()) {
        const std::uint8_t block = std::uint8_t(b[at]);
        if (block == 0x3B) { // the trailer: what follows is no image
            out += char(0x3B);
            return out;
        }
        if (block == 0x21) {
            if (at + 2 > b.size())
                return std::nullopt;
            const std::uint8_t label = std::uint8_t(b[at + 1]);
            const std::size_t end = skipSubBlocks(b, at + 2);
            if (end == std::string_view::npos)
                return std::nullopt;
            bool keep = label != 0xFE;
            if (label == 0xFF) { // an application's: only the loop count
                const std::string_view id = at + 14 <= b.size() ? b.substr(at + 3, 11) : std::string_view();
                keep = id == "NETSCAPE2.0" || id == "ANIMEXTS1.0";
            }
            if (keep)
                out.append(b.substr(at, end - at));
            at = end;
        } else if (block == 0x2C) {
            if (at + 10 > b.size())
                return std::nullopt;
            const std::uint8_t local = std::uint8_t(b[at + 9]);
            std::size_t data = at + 10 + ((local & 0x80) ? 3 * (std::size_t(1) << ((local & 7) + 1)) : 0);
            const std::size_t end = data < b.size() ? skipSubBlocks(b, data + 1) : std::string_view::npos;
            if (end == std::string_view::npos)
                return std::nullopt;
            out.append(b.substr(at, end - at));
            at = end;
        } else {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// ── MP4 and MOV ───────────────────────────────────────────────────────────

/// Overwrites the metadata boxes in [begin, end) of @p out as free boxes,
/// going into the boxes that hold others. False when a box overruns.
bool neutralizeBoxes(std::string& out, std::size_t begin, std::size_t end, int depth)
{
    std::size_t at = begin;
    while (at + 8 <= end) {
        std::uint64_t size = be32(out, at);
        std::size_t header = 8;
        if (size == 1) {
            if (at + 16 > end)
                return false;
            size = be64(out, at + 8);
            header = 16;
        } else if (size == 0) {
            size = end - at;
        }
        if (size < header || at + size > end)
            return false;
        const std::string type = out.substr(at + 4, 4);
        if (type == "udta" || type == "meta" || type == "uuid" || type == "XMP_") {
            std::memcpy(out.data() + at + 4, "free", 4);
            std::memset(out.data() + at + header, 0, std::size_t(size) - header);
        } else if (depth < 8
                   && (type == "moov" || type == "trak" || type == "mdia" || type == "minf" || type == "edts")) {
            if (!neutralizeBoxes(out, at + header, at + std::size_t(size), depth + 1))
                return false;
        }
        at += std::size_t(size);
    }
    return true;
}

std::optional<std::string> stripMp4(std::string_view b)
{
    if (b.size() < 12 || b.substr(4, 4) != "ftyp")
        return std::nullopt;
    std::string out(b);
    if (!neutralizeBoxes(out, 0, out.size(), 0))
        return std::nullopt;
    return out;
}

// ── WebM ──────────────────────────────────────────────────────────────────

/// An EBML variable-length number at @p at: its value and length; for an ID
/// the marker bit is kept.
bool vint(std::string_view b, std::size_t at, bool keepMarker, std::uint64_t& value, std::size_t& length)
{
    if (at >= b.size())
        return false;
    const std::uint8_t first = std::uint8_t(b[at]);
    length = 1;
    while (length <= 8 && !(first & (0x80 >> (length - 1))))
        ++length;
    if (length > 8 || at + length > b.size())
        return false;
    value = keepMarker ? first : (first & (0xFF >> length));
    for (std::size_t i = 1; i < length; ++i)
        value = (value << 8) | std::uint8_t(b[at + i]);
    return true;
}

std::optional<std::string> stripWebm(std::string_view b)
{
    if (!startsWith(b, "\x1A\x45\xDF\xA3"))
        return std::nullopt;
    std::string out(b);
    std::size_t at = 0;
    while (at < out.size()) {
        std::uint64_t id = 0, size = 0;
        std::size_t idLength = 0, sizeLength = 0;
        if (!vint(out, at, true, id, idLength) || !vint(out, at + idLength, false, size, sizeLength))
            return std::nullopt;
        const std::size_t header = idLength + sizeLength;
        const bool unknownSize = size == (std::uint64_t(1) << (7 * sizeLength)) - 1;
        if (id == 0x18538067) { // the Segment: go into it
            at += header;
            continue;
        }
        if (unknownSize)
            return out; // a cluster of unknown size: nothing past it can be found
        if (at + header + size > out.size())
            return std::nullopt;
        if ((id == 0x1254C367 || id == 0x1941A469) && header + size >= 9) { // Tags, Attachments
            // One Void element of the same length: its ID, an 8-byte size, zeros.
            const std::uint64_t total = header + size;
            const std::uint64_t voidSize = total - 9;
            out[at] = char(0xEC);
            out[at + 1] = char(0x01);
            for (int i = 0; i < 7; ++i)
                out[at + 2 + std::size_t(i)] = char((voidSize >> (8 * (6 - i))) & 0xFF);
            std::memset(out.data() + at + 9, 0, std::size_t(voidSize));
        }
        at += header + std::size_t(size);
    }
    return out;
}

} // namespace

std::optional<Type> sniff(std::string_view b, bool textName)
{
    if (startsWith(b, std::string_view("\x89PNG\r\n\x1a\n", 8)))
        return Type{Kind::Image, "png"};
    if (startsWith(b, "\xFF\xD8\xFF"))
        return Type{Kind::Image, "jpg"};
    if (startsWith(b, "GIF87a") || startsWith(b, "GIF89a"))
        return Type{Kind::Image, "gif"};
    if (b.size() >= 12 && startsWith(b, "RIFF") && b.substr(8, 4) == "WEBP")
        return Type{Kind::Image, "webp"};
    if (b.size() >= 12 && b.substr(4, 4) == "ftyp")
        return Type{Kind::Video, b.substr(8, 4) == "qt  " ? "mov" : "mp4"};
    if (startsWith(b, "\x1A\x45\xDF\xA3") && b.substr(0, 64).find("webm") != std::string_view::npos)
        return Type{Kind::Video, "webm"};
    if (startsWith(b, "PK\x03\x04"))
        return Type{Kind::Archive, "zip"};
    if (startsWith(b, "\x1F\x8B"))
        return Type{Kind::Archive, "gz"};
    if (startsWith(b,
                   std::string_view("\xFD"
                                    "7zXZ\0",
                                    6)))
        return Type{Kind::Archive, "xz"};
    if (startsWith(b, "\x28\xB5\x2F\xFD"))
        return Type{Kind::Archive, "zst"};
    if (startsWith(b, "7z\xBC\xAF\x27\x1C"))
        return Type{Kind::Archive, "7z"};
    if (textName && b.find('\0') == std::string_view::npos)
        return Type{Kind::Text, "txt"};
    return std::nullopt;
}

std::optional<std::string> strip(std::string_view bytes, const Type& type)
{
    if (type.extension == "jpg")
        return stripJpeg(bytes);
    if (type.extension == "png")
        return stripPng(bytes);
    if (type.extension == "webp")
        return stripWebp(bytes);
    if (type.extension == "gif")
        return stripGif(bytes);
    if (type.extension == "mp4" || type.extension == "mov")
        return stripMp4(bytes);
    if (type.extension == "webm")
        return stripWebm(bytes);
    if (type.kind == Kind::Text)
        return scrubPaths(bytes);
    return std::string(bytes);
}

std::string scrubPaths(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    const auto nameEnd = [&](std::size_t at) {
        while (at < text.size() && text[at] != '/' && text[at] != '\\'
               && !std::isspace(static_cast<unsigned char>(text[at])) && text[at] != '"' && text[at] != '\''
               && text[at] != ':')
            ++at;
        return at;
    };
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t prefix = 0;
        if (text.compare(i, 6, "/home/") == 0)
            prefix = 6;
        else if (text.compare(i, 7, "/Users/") == 0)
            prefix = 7;
        else if (i + 9 <= text.size() && std::isalpha(static_cast<unsigned char>(text[i])) && text[i + 1] == ':'
                 && (text[i + 2] == '\\' || text[i + 2] == '/')
                 && (text.compare(i + 3, 6, "Users\\") == 0 || text.compare(i + 3, 6, "Users/") == 0)
                 && (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1]))))
            prefix = 9; // a drive's letter is no letter of a word
        if (prefix) {
            const std::size_t end = nameEnd(i + prefix);
            if (end > i + prefix) {
                out += '~';
                i = end;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

} // namespace Glim::Media
