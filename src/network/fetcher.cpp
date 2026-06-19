#include "network/fetcher.h"
#include <windows.h>
#include <wininet.h>
#include <cctype>
#pragma comment(lib, "wininet.lib")

static int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static std::string PercentDecode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            int hi = HexValue(input[i + 1]);
            int lo = HexValue(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += (input[i] == '+') ? ' ' : input[i];
    }
    return out;
}

static int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + c - 'a';
    if (c >= '0' && c <= '9') return 52 + c - '0';
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::string Base64Decode(const std::string& input) {
    std::string out;
    int val = 0;
    int bits = -8;
    for (char c : input) {
        if (std::isspace((unsigned char)c)) continue;
        if (c == '=') break;
        int b = Base64Value(c);
        if (b < 0) continue;
        val = (val << 6) | b;
        bits += 6;
        if (bits >= 0) {
            out += (char)((val >> bits) & 0xff);
            bits -= 8;
        }
    }
    return out;
}

static bool StartsWithNoCase(const std::string& value, const char* prefix) {
    for (size_t i = 0; prefix[i]; ++i) {
        if (i >= value.size()) return false;
        if (std::tolower((unsigned char)value[i]) != std::tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

FetchResult FetchUrl(const std::string& url) {
    FetchResult r;

    if (StartsWithNoCase(url, "data:")) {
        size_t comma = url.find(',');
        if (comma == std::string::npos) {
            r.error = "Malformed data URL";
            return r;
        }
        std::string meta = url.substr(5, comma - 5);
        std::string payload = url.substr(comma + 1);
        bool base64 = false;
        r.contentType = "text/plain";

        size_t start = 0;
        while (start <= meta.size()) {
            size_t semi = meta.find(';', start);
            std::string part = meta.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
            if (!part.empty()) {
                std::string low;
                for (char c : part) low += (char)std::tolower((unsigned char)c);
                if (low == "base64") base64 = true;
                else if (part.find('/') != std::string::npos) r.contentType = part;
            }
            if (semi == std::string::npos) break;
            start = semi + 1;
        }

        std::string decoded = PercentDecode(payload);
        r.body = base64 ? Base64Decode(decoded) : decoded;
        r.finalUrl = url;
        r.success = true;
        return r;
    }

    HINTERNET hNet = InternetOpenA(
        "Helix/0.1",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr, nullptr, 0);
    if (!hNet) { r.error = "InternetOpen failed"; return r; }

    DWORD flags =
        INTERNET_FLAG_RELOAD |
        INTERNET_FLAG_NO_CACHE_WRITE |
        INTERNET_FLAG_IGNORE_CERT_DATE_INVALID |
        INTERNET_FLAG_IGNORE_CERT_CN_INVALID;  // tolerate self-signed in v0.1

    HINTERNET hReq = InternetOpenUrlA(hNet, url.c_str(), nullptr, 0, flags, 0);
    if (!hReq) {
        r.error = "InternetOpenUrl failed for: " + url;
        InternetCloseHandle(hNet);
        return r;
    }

    // Resolve final URL (after redirects)
    char finalUrl[2048] = {};
    DWORD len = (DWORD)sizeof(finalUrl);
    HttpQueryInfoA(hReq, HTTP_QUERY_LOCATION, finalUrl, &len, nullptr);
    r.finalUrl = *finalUrl ? std::string(finalUrl) : url;

    // Content-Type header
    char ct[256] = {};
    len = (DWORD)sizeof(ct);
    HttpQueryInfoA(hReq, HTTP_QUERY_CONTENT_TYPE, ct, &len, nullptr);
    r.contentType = ct;

    // Read body
    char buf[8192];
    DWORD bytesRead = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf), &bytesRead) && bytesRead)
        r.body.append(buf, bytesRead);

    InternetCloseHandle(hReq);
    InternetCloseHandle(hNet);
    r.success = true;
    return r;
}
