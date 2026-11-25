// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>

/// What Glim publishes of a file someone attached: its kind, read from its
/// bytes and never from its name, and its bytes without what they say of the
/// person (a photo's place and camera, a video's location).
namespace Glim::Media {

enum class Kind {
    Image, ///< shown in the issue
    Video, ///< linked
    Text, ///< put in the issue, and linked whole
    Archive, ///< linked, downloaded
};

struct Type
{
    Kind kind;
    std::string extension; ///< png, jpg, gif, webp, mp4, mov, webm, txt, zip, gz, xz, zst, 7z
};

/// The type of @p bytes, from their signature; a text file only when
/// @p textName says so and the bytes hold no NUL. Nothing for any other file.
std::optional<Type> sniff(std::string_view bytes, bool textName);

/// @p bytes without their metadata:
///
/// - JPEG: EXIF, XMP and IPTC (APP1, APP13 and the other APPn but JFIF, ICC
///   and Adobe), comments, and what follows the image's end (a phone's motion
///   photo). The EXIF orientation is kept, alone, so the photo stays upright.
/// - PNG: its text, its time and its EXIF chunks, and what follows IEND.
/// - WebP: its EXIF and XMP chunks.
/// - GIF: comments, and applications' data but the loop count.
/// - MP4 and MOV: their udta, meta and uuid boxes, overwritten as free boxes
///   (the media's offsets stay right).
/// - WebM: its Tags and Attachments, overwritten as Void.
///
/// Nothing when the bytes are not what their type says: such a file is not
/// published. Text and archives come back as they are.
std::optional<std::string> strip(std::string_view bytes, const Type& type);

/// A text file's home folders written as "~": /home/NAME, /Users/NAME,
/// C:\Users\NAME (any drive), as a log names its user.
std::string scrubPaths(std::string_view text);

} // namespace Glim::Media
