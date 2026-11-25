// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Media.h"

#include <gtest/gtest.h>

#include <cstdint>

using namespace Glim::Media;

namespace {

std::string bytes(std::initializer_list<int> values)
{
    std::string out;
    for (const int v : values)
        out += char(v);
    return out;
}

std::string be16(std::size_t v)
{
    return bytes({int((v >> 8) & 0xFF), int(v & 0xFF)});
}

std::string be32(std::size_t v)
{
    return bytes({int((v >> 24) & 0xFF), int((v >> 16) & 0xFF), int((v >> 8) & 0xFF), int(v & 0xFF)});
}

std::string le32(std::size_t v)
{
    return bytes({int(v & 0xFF), int((v >> 8) & 0xFF), int((v >> 16) & 0xFF), int((v >> 24) & 0xFF)});
}

std::string segment(int marker, const std::string& payload)
{
    return bytes({0xFF, marker}) + be16(payload.size() + 2) + payload;
}

/// EXIF (little-endian) with an orientation and something that must not leave.
std::string exif(int orientation)
{
    std::string tiff = "II" + bytes({42, 0, 8, 0, 0, 0});
    tiff += bytes({2, 0}); // two entries
    tiff += bytes({0x12, 0x01, 3, 0, 1, 0, 0, 0, orientation, 0, 0, 0}); // Orientation
    tiff += bytes({0x25, 0x88, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0}); // GPSInfo
    tiff += bytes({0, 0, 0, 0});
    return std::string("Exif\0\0", 6) + tiff + "GPS 40.7128N 74.0060W";
}

std::string jpeg()
{
    return "\xFF\xD8" + segment(0xE0, std::string("JFIF\0\1\1\0\0\1\0\1\0\0", 14)) + segment(0xE1, exif(6))
        + segment(0xED, "Photoshop 3.0 IPTC byline Someone") + segment(0xFE, "a comment that names someone")
        + segment(0xDB, std::string(65, '\x01')) + segment(0xDA, std::string(10, '\x02'))
        + bytes({0x12, 0xFF, 0x00, 0x34, 0xFF, 0xD0, 0x56}) + "\xFF\xD9" + "MOTIONPHOTO-VIDEO-BYTES";
}

std::string pngChunk(const std::string& type, const std::string& data)
{
    return be32(data.size()) + type + data + be32(0);
}

std::string png()
{
    return std::string("\x89PNG\r\n\x1a\n", 8) + pngChunk("IHDR", std::string(13, '\x01'))
        + pngChunk("tEXt", std::string("Author\0Someone", 14)) + pngChunk("eXIf", "GPS secret")
        + pngChunk("tIME", std::string(7, '\x07')) + pngChunk("IDAT", "pixels") + pngChunk("IEND", "") + "TRAILER";
}

std::string riffChunk(const std::string& fourcc, const std::string& data)
{
    std::string out = fourcc + le32(data.size()) + data;
    if (data.size() & 1)
        out += '\0';
    return out;
}

std::string webp()
{
    const std::string body = "WEBP" + riffChunk("VP8X", bytes({0x0C, 0, 0, 0, 1, 0, 0, 1, 0, 0}))
        + riffChunk("VP8 ", "frame") + riffChunk("EXIF", "GPS secret") + riffChunk("XMP ", "<x:creator>Someone");
    return "RIFF" + le32(body.size()) + body;
}

std::string gif()
{
    std::string out = "GIF89a" + bytes({1, 0, 1, 0, 0, 0, 0}); // no global table
    out += bytes({0x21, 0xFE, 7}) + "Someone" + bytes({0}); // comment
    out += bytes({0x21, 0xFF, 11}) + "NETSCAPE2.0" + bytes({3, 1, 0, 0, 0}); // the loop
    out += bytes({0x21, 0xFF, 11}) + "XMP DataXMP" + bytes({6}) + "secret" + bytes({0});
    out += bytes({0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x4C, 0x01, 0}); // the image
    out += bytes({0x3B}) + "TRAILER";
    return out;
}

std::string box(const std::string& type, const std::string& data)
{
    return be32(data.size() + 8) + type + data;
}

std::string mp4()
{
    return box("ftyp", "isom\0\0\0\0isom") + box("mdat", "MEDIA")
        + box("moov",
              box("mvhd", std::string(20, '\x01')) + box("udta", box("\xA9xyz", "+40.7128-074.0060/"))
                  + box("trak", box("tkhd", "track") + box("udta", "phone model"))
                  + box("meta", "com.apple.quicktime.location.ISO6709"))
        + box("uuid", "XMP creator Someone");
}

std::string webm()
{
    const std::string header = bytes({0x1A, 0x45, 0xDF, 0xA3, 0x84}) + "webm";
    const std::string info = bytes({0x15, 0x49, 0xA9, 0x66, 0x83}) + "abc";
    const std::string tags = bytes({0x12, 0x54, 0xC3, 0x67, 0x90}) + "TITLE by Someone";
    const std::string cluster = bytes({0x1F, 0x43, 0xB6, 0x75, 0x85}) + "media";
    return header + bytes({0x18, 0x53, 0x80, 0x67, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}) + info + tags
        + cluster;
}

bool has(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(Media, KindsComeFromTheBytes)
{
    EXPECT_EQ(sniff(png(), false)->extension, "png");
    EXPECT_EQ(sniff(jpeg(), false)->extension, "jpg");
    EXPECT_EQ(sniff(gif(), false)->extension, "gif");
    EXPECT_EQ(sniff(webp(), false)->extension, "webp");
    EXPECT_EQ(sniff(mp4(), false)->kind, Kind::Video);
    EXPECT_EQ(sniff(webm(), false)->extension, "webm");
    EXPECT_EQ(sniff("PK\x03\x04rest", false)->kind, Kind::Archive);
    EXPECT_EQ(sniff("log line", true)->kind, Kind::Text);
    EXPECT_FALSE(sniff("log line", false)) << "text only by a text file's name";
    EXPECT_FALSE(sniff(std::string("bin\0ary", 7), true)) << "a NUL is no text";
    EXPECT_FALSE(sniff("<svg onload=alert(1)>", false)) << "nothing a browser runs";
    EXPECT_FALSE(sniff("MZ\x90\x00 an executable", false));
}

TEST(Media, AJpegKeepsItsPictureAndItsOrientationAlone)
{
    const auto out = strip(jpeg(), *sniff(jpeg(), false));
    ASSERT_TRUE(out);
    EXPECT_FALSE(has(*out, "GPS")) << "the EXIF";
    EXPECT_FALSE(has(*out, "IPTC"));
    EXPECT_FALSE(has(*out, "names someone")) << "the comment";
    EXPECT_FALSE(has(*out, "MOTIONPHOTO")) << "what follows the image's end";
    EXPECT_TRUE(out->starts_with("\xFF\xD8\xFF\xE0"));
    EXPECT_TRUE(out->ends_with("\xFF\xD9"));
    EXPECT_TRUE(has(*out, std::string("Exif\0\0MM\0\x2A", 10))) << "a new EXIF of the orientation";
    EXPECT_TRUE(has(*out, bytes({0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, 6})));
    EXPECT_TRUE(has(*out, bytes({0x12, 0xFF, 0x00, 0x34, 0xFF, 0xD0, 0x56}))) << "the scan's bytes";
}

TEST(Media, AnUprightJpegGetsNoExif)
{
    std::string upright = jpeg();
    const std::string withSix = exif(6), withOne = exif(1);
    upright.replace(upright.find(withSix), withSix.size(), withOne);
    const auto out = strip(upright, *sniff(upright, false));
    ASSERT_TRUE(out);
    EXPECT_FALSE(has(*out, "Exif"));
}

TEST(Media, ABrokenJpegIsNotPublished)
{
    EXPECT_FALSE(strip("\xFF\xD8\xFF\xE0\x00\x10JFIF", Type{Kind::Image, "jpg"}));
    EXPECT_FALSE(strip(jpeg().substr(0, 60), Type{Kind::Image, "jpg"}));
}

TEST(Media, APngLosesItsTextAndTime)
{
    const auto out = strip(png(), *sniff(png(), false));
    ASSERT_TRUE(out);
    EXPECT_FALSE(has(*out, "Someone"));
    EXPECT_FALSE(has(*out, "GPS"));
    EXPECT_FALSE(has(*out, "tIME"));
    EXPECT_FALSE(has(*out, "TRAILER"));
    EXPECT_TRUE(has(*out, "IHDR"));
    EXPECT_TRUE(has(*out, "pixels"));
    EXPECT_TRUE(out->ends_with("IEND" + be32(0)));
}

TEST(Media, AWebpLosesItsExifAndXmp)
{
    const auto out = strip(webp(), *sniff(webp(), false));
    ASSERT_TRUE(out);
    EXPECT_FALSE(has(*out, "GPS"));
    EXPECT_FALSE(has(*out, "Someone"));
    EXPECT_TRUE(has(*out, "frame"));
    const std::size_t riff = std::uint8_t((*out)[4]) | (std::uint8_t((*out)[5]) << 8);
    EXPECT_EQ(riff + 8, out->size()) << "the RIFF size follows";
    EXPECT_EQ((*out)[20] & 0x0C, 0) << "VP8X no more says EXIF or XMP";
}

TEST(Media, AGifKeepsOnlyItsLoop)
{
    const auto out = strip(gif(), *sniff(gif(), false));
    ASSERT_TRUE(out);
    EXPECT_FALSE(has(*out, "Someone"));
    EXPECT_FALSE(has(*out, "secret"));
    EXPECT_FALSE(has(*out, "TRAILER"));
    EXPECT_TRUE(has(*out, "NETSCAPE2.0"));
    EXPECT_TRUE(out->ends_with("\x3B"));
}

TEST(Media, AnMp4LosesItsPlaceButKeepsItsOffsets)
{
    const std::string in = mp4();
    const auto out = strip(in, *sniff(in, false));
    ASSERT_TRUE(out);
    EXPECT_EQ(out->size(), in.size()) << "the media's offsets stay right";
    EXPECT_FALSE(has(*out, "+40.7128"));
    EXPECT_FALSE(has(*out, "phone model"));
    EXPECT_FALSE(has(*out, "location"));
    EXPECT_FALSE(has(*out, "Someone"));
    EXPECT_TRUE(has(*out, "MEDIA"));
    EXPECT_TRUE(has(*out, "mvhd"));
    EXPECT_TRUE(has(*out, "free"));
}

TEST(Media, AWebmLosesItsTags)
{
    const std::string in = webm();
    const auto out = strip(in, *sniff(in, false));
    ASSERT_TRUE(out);
    EXPECT_EQ(out->size(), in.size());
    EXPECT_FALSE(has(*out, "Someone"));
    EXPECT_TRUE(has(*out, "media"));
    EXPECT_TRUE(has(*out, "abc"));
}

TEST(Media, HomeFoldersBecomeTilde)
{
    EXPECT_EQ(scrubPaths("open /home/jane/.config/kwinrc"), "open ~/.config/kwinrc");
    EXPECT_EQ(scrubPaths("/Users/Jane Doe/x"), "~ Doe/x") << "a name ends at a space";
    EXPECT_EQ(scrubPaths("D:\\Users\\jane\\AppData"), "~\\AppData");
    EXPECT_EQ(scrubPaths("/srv/home/jane"), "/srv~") << "any /home/";
    EXPECT_EQ(scrubPaths("nothing here"), "nothing here");
    EXPECT_EQ(scrubPaths("/home/"), "/home/");
}
