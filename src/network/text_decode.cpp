#include "network/text_decode.h"

#include <windows.h>

#include <algorithm>
#include <cctype>

namespace {
std::string LowerAscii(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string Trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
        [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string CharsetFrom(const std::string& source) {
    const std::string lower = LowerAscii(source);
    const size_t marker = lower.find("charset");
    if (marker == std::string::npos) return {};
    size_t cursor = marker + 7;
    while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
    if (cursor >= source.size() || source[cursor] != '=') return {};
    ++cursor;
    while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
    const char quote = cursor < source.size() && (source[cursor] == '\'' || source[cursor] == '"')
        ? source[cursor++] : 0;
    const size_t end = source.find_first_of(quote ? std::string(1, quote) : " \t\r\n;>", cursor);
    return LowerAscii(Trim(source.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor)));
}

std::string Utf8FromWide(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::string DecodeCodePage(const std::string& bytes, UINT codePage) {
    if (bytes.empty()) return {};
    const int wideLength = MultiByteToWideChar(codePage, 0, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLength <= 0) return bytes;
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
        wide.data(), wideLength);
    const std::string utf8 = Utf8FromWide(wide);
    return utf8.empty() ? bytes : utf8;
}

std::string DecodeUtf16(const std::string& bytes, bool littleEndian) {
    if (bytes.size() < 2) return {};
    std::wstring wide;
    wide.reserve(bytes.size() / 2);
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const unsigned char a = static_cast<unsigned char>(bytes[i]);
        const unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
        wide += static_cast<wchar_t>(littleEndian ? (a | (b << 8)) : ((a << 8) | b));
    }
    return Utf8FromWide(wide);
}
} // namespace

std::string DecodeTextToUtf8(const std::string& bytes,
                             const std::string& contentType,
                             bool sniffHtmlCharset) {
    if (bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xef
        && static_cast<unsigned char>(bytes[1]) == 0xbb
        && static_cast<unsigned char>(bytes[2]) == 0xbf)
        return bytes.substr(3);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff
        && static_cast<unsigned char>(bytes[1]) == 0xfe)
        return DecodeUtf16(bytes.substr(2), true);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xfe
        && static_cast<unsigned char>(bytes[1]) == 0xff)
        return DecodeUtf16(bytes.substr(2), false);

    std::string charset = CharsetFrom(contentType);
    if (charset.empty() && sniffHtmlCharset)
        charset = CharsetFrom(bytes.substr(0, std::min<size_t>(bytes.size(), 8192)));
    if (charset.empty() || charset == "utf-8" || charset == "utf8")
        return bytes;
    if (charset == "utf-16" || charset == "utf-16le")
        return DecodeUtf16(bytes, true);
    if (charset == "utf-16be")
        return DecodeUtf16(bytes, false);
    if (charset == "windows-1252" || charset == "cp1252"
        || charset == "iso-8859-1" || charset == "latin1")
        return DecodeCodePage(bytes, 1252);
    return bytes; // Unknown encoding: preserve bytes rather than corrupt them.
}
