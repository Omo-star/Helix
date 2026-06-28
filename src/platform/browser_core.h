#pragma once
//
// browser_core.h — platform-independent browser logic.
//
// Contains: Tab management, navigation, history, URL encoding, home page HTML,
// image fetch throttling. Used by all platform shells (Win32, macOS, Linux).
//
#include "network/fetcher.h"
#include "network/url.h"
#include "network/text_decode.h"
#include "html/parser.h"
#include "html/resources.h"
#include "js/engine.h"
#include "layout/box.h"

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <algorithm>
#include <cctype>

// ── semaphore (C++17 compat) ─────────────────────────────────────────────────

class Semaphore {
public:
    explicit Semaphore(int count) : m_count(count) {}
    void acquire() {
        std::unique_lock<std::mutex> lk(m_mu);
        m_cv.wait(lk, [&] { return m_count > 0; });
        --m_count;
    }
    void release() {
        { std::lock_guard<std::mutex> lk(m_mu); ++m_count; }
        m_cv.notify_one();
    }
private:
    std::mutex m_mu;
    std::condition_variable m_cv;
    int m_count;
};

// ── data types ───────────────────────────────────────────────────────────────

struct Page {
    std::string           url;
    std::shared_ptr<Node> dom;
    std::string           error;
};

struct ImageMsg {
    std::string          url;
    std::vector<uint8_t> bytes;
};

struct PageMsg {
    int   tabIdx;
    Page* page;
};

struct Tab {
    std::string           url        = "helix://home";
    std::string           displayUrl;
    std::string           title      = "Helix";
    std::shared_ptr<Page> page;
    float                 scrollY    = 0.f;
    float                 docHeight  = 600.f;
    bool                  loading    = false;
    std::string           pendingFragment;
    bool                  fragmentScrollPending = false;
    std::vector<std::string> history;
    int                   histIdx    = -1;
};

// ── URL helpers ──────────────────────────────────────────────────────────────

inline std::string UrlEncodeQuery(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else {
            char hex[4]; snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

inline bool LooksLikeUrl(const std::string& s) {
    if (s.empty()) return false;
    if (s.find("://") != std::string::npos) return true;
    if (s.size() > 1 && s[0] == '/' && s[1] == '/') return true;
    if (s.rfind("helix://", 0) == 0 || s.rfind("about:", 0) == 0) return true;
    if (s.find(' ') != std::string::npos) return false;
    // localhost, localhost:3000
    if (s == "localhost" || s.rfind("localhost:", 0) == 0) return true;
    // 127.0.0.1, 127.0.0.1:8080
    if (s.rfind("127.", 0) == 0) return true;
    // [::1]
    if (s.rfind("[", 0) == 0) return true;
    // IP addresses (digits and dots only, no TLD needed)
    bool allDigitsDots = true;
    for (char c : s) if (!std::isdigit((unsigned char)c) && c != '.' && c != ':') { allDigitsDots = false; break; }
    if (allDigitsDots && s.find('.') != std::string::npos) return true;
    // Has a dot → probably a domain (example.com, en.wikipedia.org)
    size_t dot = s.find('.');
    if (dot == std::string::npos || dot == 0 || dot == s.size() - 1) return false;
    return true;
}

inline std::string UrlFragment(const std::string& url) {
    size_t hash = url.find('#');
    if (hash == std::string::npos || hash + 1 >= url.size()) return {};
    return url.substr(hash + 1);
}

inline std::string UrlWithoutFragment(const std::string& url) {
    size_t hash = url.find('#');
    return hash == std::string::npos ? url : url.substr(0, hash);
}

inline void TabPushHistory(Tab& tab, const std::string& url) {
    if (tab.histIdx >= 0 && tab.histIdx < (int)tab.history.size())
        tab.history.erase(tab.history.begin() + tab.histIdx + 1, tab.history.end());
    tab.history.push_back(url);
    tab.histIdx = (int)tab.history.size() - 1;
}

// ── home page ────────────────────────────────────────────────────────────────

inline const std::string& HomePageHtml() {
    static const std::string html = R"html(<!DOCTYPE html>
<html>
<head><title>Helix</title>
<style>
body {
    font-family: 'Segoe UI', system-ui, sans-serif;
    background: #f5f6f8;
    color: #17191f;
    margin: 0;
    padding: 56px 0;
    line-height: 1.45;
}
.w { width: 720px; margin-left: auto; margin-right: auto; }
.brand { text-align: center; margin-bottom: 26px; }
.mark {
    display: inline-block; background: #3b6df6; color: white;
    border-radius: 10px; padding: 7px 13px; font-weight: 700;
    letter-spacing: 0; font-size: 15px;
}
h1 { font-size: 44px; color: #17191f; text-align: center; margin: 14px 0 4px; }
.sub { text-align: center; color: #8a93a5; font-size: 15px; margin: 0; }
.search {
    background: #ffffff; border: 1px solid #d1d6e0; border-radius: 10px;
    padding: 15px 18px; margin: 24px 0 18px; color: #8a93a5;
    font-size: 16px;
}
.search strong { color: #17191f; }
.section-title {
    color: #8a93a5; font-size: 12px; font-weight: 700;
    margin: 22px 0 10px; text-transform: uppercase;
}
.links { padding: 0; }
.links a {
    display: block; background: #ffffff; border: 1px solid #d1d6e0;
    border-radius: 8px; padding: 14px 16px; margin: 8px 0;
    text-decoration: none; color: #17191f; font-size: 15px;
}
.links a strong { color: #3b6df6; }
.url { color: #8a93a5; font-size: 12px; }
.shortcuts {
    background: #e5e8ee; border-radius: 8px; padding: 14px 18px;
    margin-top: 18px;
}
.key { display: block; padding: 4px 0; color: #5d6575; font-size: 13px; }
.key strong { color: #17191f; }
.ft { margin-top: 24px; text-align: center; }
.ft p { font-size: 12px; color: #8a93a5; }
.tag { background: #dde7ff; color: #3b6df6; border-radius: 4px; padding: 2px 8px; font-size: 11px; }
</style>
</head>
<body>
<div class="w">
<div class="brand">
<span class="mark">HELIX</span>
<h1>Start browsing</h1>
<p class="sub">A from-scratch browser engine with its own HTML, CSS, JS, layout, and renderer.</p>
</div>
<div class="search"><strong>Ctrl+L</strong> to search or enter a URL</div>
<div class="section-title">Quick links</div>
<div class="links">
<a href="https://www.wikipedia.org/"><strong>Wikipedia</strong> <span class="url">www.wikipedia.org</span></a>
<a href="https://news.ycombinator.com"><strong>Hacker News</strong> <span class="url">news.ycombinator.com</span></a>
<a href="https://lite.cnn.com"><strong>CNN Lite</strong> <span class="url">lite.cnn.com</span></a>
<a href="helix://history"><strong>History</strong> <span class="url">helix://history</span></a>
</div>
<div class="shortcuts">
<div class="section-title" style="margin-top:0;">Shortcuts</div>
<span class="key"><strong>Ctrl+L</strong> &mdash; address bar</span>
<span class="key"><strong>Ctrl+T / W</strong> &mdash; new / close tab</span>
<span class="key"><strong>Ctrl+R</strong> &mdash; reload</span>
<span class="key"><strong>Ctrl+F</strong> &mdash; find in page</span>
<span class="key"><strong>Ctrl + + / -</strong> &mdash; zoom</span>
<span class="key"><strong>Alt+Left/Right</strong> &mdash; back / forward</span>
</div>
<div class="ft">
<p><span class="tag">cross-platform</span></p>
<p style="margin-top:6px;">No Chromium. No WebView. No shortcuts.</p>
</div>
</div>
</body>
</html>)html";
    return html;
}

// ── shared helpers for page loading ──────────────────────────────────────────

inline std::string LowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

inline bool AttrContainsToken(const std::string& value, const std::string& token) {
    std::string lower = LowerAscii(value);
    size_t start = 0;
    while (start < lower.size()) {
        while (start < lower.size() && std::isspace((unsigned char)lower[start])) ++start;
        size_t end = start;
        while (end < lower.size() && !std::isspace((unsigned char)lower[end])) ++end;
        if (lower.substr(start, end - start) == token) return true;
        start = end;
    }
    return false;
}

inline bool StylesheetMediaApplies(const std::string& media) {
    std::string lower = LowerAscii(media);
    if (lower.empty()) return true;
    return lower.find("all") != std::string::npos
        || lower.find("screen") != std::string::npos
        || lower.find("projection") != std::string::npos;
}

inline Node* FindFirstElement(Node* root, const std::string& tag) {
    if (!root) return nullptr;
    std::vector<Node*> stack{ root };
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        if (n->type == NodeType::Element && n->tagName == tag) return n;
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
            stack.push_back(it->get());
    }
    return nullptr;
}

// Resolve @import url(...) inside a CSS string and return the combined CSS.
inline std::string ResolveImports(const std::string& css, const std::string& baseUrl,
                                   int& loaded, size_t& loadedBytes, int depth = 0) {
    if (depth > 3) return css;  // prevent infinite @import chains
    std::string result;
    size_t pos = 0;
    while (pos < css.size()) {
        // Look for @import
        size_t importAt = css.find("@import", pos);
        if (importAt == std::string::npos) { result += css.substr(pos); break; }
        result += css.substr(pos, importAt - pos);
        size_t urlStart = css.find_first_of("\"'(", importAt + 7);
        if (urlStart == std::string::npos) { result += css.substr(importAt); break; }
        std::string importUrl;
        size_t urlEnd;
        if (css[urlStart] == '(') {
            urlEnd = css.find(')', urlStart + 1);
            if (urlEnd == std::string::npos) { result += css.substr(importAt); break; }
            importUrl = css.substr(urlStart + 1, urlEnd - urlStart - 1);
            urlEnd = css.find(';', urlEnd);
        } else {
            char q = css[urlStart];
            urlEnd = css.find(q, urlStart + 1);
            if (urlEnd == std::string::npos) { result += css.substr(importAt); break; }
            importUrl = css.substr(urlStart + 1, urlEnd - urlStart - 1);
            urlEnd = css.find(';', urlEnd);
        }
        // Strip url() wrapper and quotes
        if (importUrl.rfind("url(", 0) == 0) importUrl = importUrl.substr(4);
        while (!importUrl.empty() && (importUrl.back() == ')' || importUrl.back() == '"' || importUrl.back() == '\''))
            importUrl.pop_back();
        while (!importUrl.empty() && (importUrl.front() == '"' || importUrl.front() == '\''))
            importUrl.erase(importUrl.begin());
        // Fetch the imported stylesheet.
        if (!importUrl.empty() && loaded < 64 && loadedBytes < 4 * 1024 * 1024) {
            std::string resolved = ResolveUrlAgainstBase(importUrl, baseUrl);
            auto res = FetchUrl(resolved, 1024 * 1024);
            if (res.success && !res.body.empty()) {
                std::string importedCss = DecodeTextToUtf8(res.body, res.contentType);
                loadedBytes += importedCss.size();
                ++loaded;
                result += ResolveImports(importedCss, resolved, loaded, loadedBytes, depth + 1);
            }
        }
        pos = (urlEnd != std::string::npos) ? urlEnd + 1 : css.size();
    }
    return result;
}

inline void LoadExternalStylesheets(const std::shared_ptr<Node>& dom, const std::string& pageUrl) {
    if (!dom) return;
    Node* attach = FindFirstElement(dom.get(), "head");
    if (!attach) attach = dom.get();
    std::vector<Node*> stack{ dom.get() };
    int loaded = 0;
    size_t loadedBytes = 0;
    while (!stack.empty() && loaded < 64 && loadedBytes < 4 * 1024 * 1024) {
        Node* n = stack.back();
        stack.pop_back();
        if (n->type == NodeType::Element && n->tagName == "link"
            && AttrContainsToken(n->attr("rel"), "stylesheet")
            && StylesheetMediaApplies(n->attr("media"))) {
            std::string href = ResolveUrlAgainstBase(n->attr("href"), pageUrl);
            auto res = FetchUrl(href, 1024 * 1024);
            if (res.success && !res.body.empty()) {
                std::string css = DecodeTextToUtf8(res.body, res.contentType);
                loadedBytes += css.size();
                if (loadedBytes <= 4 * 1024 * 1024) {
                    // Resolve @import directives inside this stylesheet.
                    css = ResolveImports(css, href, loaded, loadedBytes);
                    auto style = Node::makeElement("style");
                    style->appendChild(Node::makeText(css));
                    attach->appendChild(style);
                    ++loaded;
                }
            }
        }
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
            stack.push_back(it->get());
    }
}
