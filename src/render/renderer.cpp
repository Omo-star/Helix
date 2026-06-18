#include "render/renderer.h"
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <sstream>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cwchar>

// ─── helpers ─────────────────────────────────────────────────────────────────

static D2D1_COLOR_F ToD2D(const CssColor& c) { return { c.r, c.g, c.b, c.a }; }
static constexpr float kMarginX = 32.f;
static constexpr float kMarginY =  8.f;

std::wstring Renderer::ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string Renderer::ResolveUrl(const std::string& href, const std::string& base) {
    if (href.empty()) return base;
    if (href.find("://") != std::string::npos) return href;
    if (href.size() >= 2 && href[0] == '/' && href[1] == '/')
        return "https:" + href;
    if (href[0] == '/') {
        size_t p = base.find("://");
        if (p == std::string::npos) return href;
        size_t sl = base.find('/', p + 3);
        return (sl == std::string::npos ? base : base.substr(0, sl)) + href;
    }
    size_t last = base.rfind('/');
    return (last == std::string::npos ? base : base.substr(0, last + 1)) + href;
}

ID2D1SolidColorBrush* Renderer::TempBrush(D2D1_COLOR_F color) {
    ID2D1SolidColorBrush* b = nullptr;
    if (m_rt) m_rt->CreateSolidColorBrush(color, &b);
    if (b) m_tempBrushes.push_back(b);
    return b;
}

IDWriteTextFormat* Renderer::TempFormat(float size, bool bold, bool mono, bool italic,
                                         const std::string& family) {
    IDWriteTextFormat* f = nullptr;
    if (!m_dwrite) return nullptr;
    std::wstring fam;
    if (!family.empty()) {
        fam = ToWide(family);
    } else {
        fam = mono ? L"Consolas" : L"Segoe UI";
    }
    m_dwrite->CreateTextFormat(
        fam.c_str(), nullptr,
        bold   ? DWRITE_FONT_WEIGHT_BOLD   : DWRITE_FONT_WEIGHT_NORMAL,
        italic ? DWRITE_FONT_STYLE_ITALIC  : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        size * m_zoom, L"en-us", &f);
    if (f) {
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_tempFormats.push_back(f);
    }
    return f;
}

// ─── init / teardown ─────────────────────────────────────────────────────────

void Renderer::RecreateFormats() {
    auto r = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    r(m_fmtBody); r(m_fmtBold); r(m_fmtItalic);
    r(m_fmtCode); r(m_fmtH1);   r(m_fmtH2); r(m_fmtH3);

    auto mk = [&](float sz, bool b, bool mono, bool it) -> IDWriteTextFormat* {
        IDWriteTextFormat* f = nullptr;
        if (!m_dwrite) return nullptr;
        m_dwrite->CreateTextFormat(
            mono ? L"Consolas" : L"Segoe UI", nullptr,
            b    ? DWRITE_FONT_WEIGHT_BOLD   : DWRITE_FONT_WEIGHT_NORMAL,
            it   ? DWRITE_FONT_STYLE_ITALIC  : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, sz * m_zoom, L"en-us", &f);
        if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        return f;
    };
    m_fmtBody   = mk(15.f, false, false, false);
    m_fmtBold   = mk(15.f, true,  false, false);
    m_fmtItalic = mk(15.f, false, false, true);
    m_fmtCode   = mk(13.f, false, true,  false);
    m_fmtH1     = mk(32.f, true,  false, false);
    m_fmtH2     = mk(24.f, true,  false, false);
    m_fmtH3     = mk(19.f, true,  false, false);
}

void Renderer::CreateTabFont() {
    if (m_fmtTab) { m_fmtTab->Release(); m_fmtTab = nullptr; }
    if (!m_dwrite) return;
    m_dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.f, L"en-us", &m_fmtTab);
    if (m_fmtTab) {
        m_fmtTab->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_fmtTab->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_fmtTab->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

bool Renderer::Init(HWND hwnd) {
    m_hwnd = hwnd;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory)))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&m_dwrite))))
        return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wic));
    RecreateFormats();
    CreateTabFont();
    return EnsureTarget();
}

bool Renderer::EnsureTarget() {
    if (m_rt) return true;
    RECT rc; GetClientRect(m_hwnd, &rc);
    m_width  = (UINT)(rc.right  - rc.left);
    m_height = (UINT)(rc.bottom - rc.top);
    if (FAILED(m_factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(m_width, m_height)),
            &m_rt)))
        return false;
    return CreateBrushes();
}

bool Renderer::CreateBrushes() {
    auto mk = [&](D2D1_COLOR_F c, ID2D1SolidColorBrush** b) {
        return SUCCEEDED(m_rt->CreateSolidColorBrush(c, b));
    };
    return mk({0,0,0,1},             &m_textBrush)
        && mk({.1f,.1f,.8f,1},       &m_linkBrush)
        && mk({.97f,.97f,.97f,1},    &m_bgBrush)
        && mk({.7f,.7f,.7f,1},       &m_hrBrush)
        && mk({.96f,.96f,.96f,1},    &m_codeBgBrush)
        && mk({1.f,.949f,.463f,1},   &m_findBrush)
        && mk({.8f,.8f,.8f,1},       &m_quoteBrush)
        && mk({.91f,.91f,.91f,1},    &m_tabBgBrush)
        && mk({1,1,1,1},             &m_tabActBrush)
        && mk({.82f,.82f,.82f,1},    &m_tabInaBrush)
        && mk({.2f,.2f,.2f,1},       &m_tabTxtBrush)
        && mk({.53f,.53f,.53f,1},    &m_tabClsBrush);
}

void Renderer::ReleaseBrushes() {
    auto r = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    r(m_textBrush); r(m_linkBrush); r(m_bgBrush); r(m_hrBrush);
    r(m_codeBgBrush); r(m_findBrush); r(m_quoteBrush);
    r(m_tabBgBrush); r(m_tabActBrush); r(m_tabInaBrush);
    r(m_tabTxtBrush); r(m_tabClsBrush);
}

void Renderer::ReleaseTarget() {
    ReleaseBrushes();
    for (auto* b : m_tempBrushes) if (b) b->Release();
    m_tempBrushes.clear();
    for (auto* f : m_tempFormats) if (f) f->Release();
    m_tempFormats.clear();
    for (auto& [url, bmp] : m_images) if (bmp) bmp->Release();
    m_images.clear();
    if (m_rt) { m_rt->Release(); m_rt = nullptr; }
}

void Renderer::DiscardTarget() { ReleaseTarget(); }

Renderer::~Renderer() {
    ReleaseTarget();
    auto r = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    r(m_fmtBody); r(m_fmtBold); r(m_fmtItalic);
    r(m_fmtCode); r(m_fmtH1);   r(m_fmtH2); r(m_fmtH3); r(m_fmtTab);
    r(m_dwrite); r(m_wic); r(m_factory);
}

void Renderer::Resize(UINT w, UINT h) {
    m_width = w; m_height = h;
    if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
}

void Renderer::SetZoom(float z) {
    m_zoom = std::max(0.5f, std::min(3.f, z));
    if (m_dwrite) RecreateFormats();
}

// ─── image loading ────────────────────────────────────────────────────────────

void Renderer::ReceiveImage(const std::string& url, const std::vector<uint8_t>& bytes) {
    if (!m_wic || !m_rt || bytes.empty()) return;
    IWICStream* stream = nullptr;
    if (FAILED(m_wic->CreateStream(&stream))) return;
    if (FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(bytes.data()), (DWORD)bytes.size()))) {
        stream->Release(); return;
    }
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(m_wic->CreateDecoderFromStream(
            stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec))) {
        stream->Release(); return;
    }
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(dec->GetFrame(0, &frame))) {
        dec->Release(); stream->Release(); return;
    }
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(m_wic->CreateFormatConverter(&conv))) {
        if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            ID2D1Bitmap* bmp = nullptr;
            if (SUCCEEDED(m_rt->CreateBitmapFromWicBitmap(conv, nullptr, &bmp))) {
                auto it = m_images.find(url);
                if (it != m_images.end() && it->second) it->second->Release();
                m_images[url] = bmp;
            }
        }
        conv->Release();
    }
    frame->Release(); dec->Release(); stream->Release();
    m_loadingImages.erase(url);
}

// ─── tab strip ───────────────────────────────────────────────────────────────

void Renderer::DrawTabStrip(const std::vector<TabEntry>& tabs, float h) {
    if (!m_rt || tabs.empty() || h < 4.f) return;
    m_tabHits.clear();

    float w      = (float)m_width;
    float newBtnW= h;
    float avail  = w - newBtnW - 4.f;
    int   n      = (int)tabs.size();
    float tabW   = std::min(200.f, std::max(80.f, avail / (float)n));
    float tabH   = h - 2.f;
    float closeW = 18.f, pad = 8.f;
    float x      = 0;

    m_rt->FillRectangle(D2D1::RectF(0, 0, w, h), m_tabBgBrush);

    for (int i = 0; i < n; i++) {
        const auto& t = tabs[i];
        auto* fill = t.active ? m_tabActBrush : m_tabInaBrush;
        m_rt->FillRectangle(D2D1::RectF(x + 1.f, 0, x + tabW - 1.f, tabH), fill);
        if (t.active)
            m_rt->DrawRectangle(D2D1::RectF(x + 1.f, 0, x + tabW - 1.f, tabH), m_hrBrush, 0.5f);

        float textW = tabW - pad * 2 - closeW;
        if (textW > 10.f && m_fmtTab) {
            std::wstring title = t.loading ? L"Loading…" : t.title;
            if (title.empty()) title = L"New Tab";
            m_rt->DrawText(title.c_str(), (UINT32)title.size(), m_fmtTab,
                D2D1::RectF(x + pad, 0, x + pad + textW, tabH),
                m_tabTxtBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        float cx = x + tabW - closeW, cy = (tabH - 14.f) / 2.f;
        if (m_fmtTab)
            m_rt->DrawText(L"×", 1, m_fmtTab,
                D2D1::RectF(cx, 0, cx + closeW, tabH), m_tabClsBrush);

        m_tabHits.push_back({ x, 0, tabW, tabH, i, false });
        m_tabHits.push_back({ cx, cy, closeW, 14.f, i, true });
        x += tabW;
    }

    float nx = avail + 4.f;
    m_rt->FillRectangle(D2D1::RectF(nx, 2.f, nx + newBtnW - 2.f, tabH - 2.f), m_tabInaBrush);
    if (m_fmtTab)
        m_rt->DrawText(L"+", 1, m_fmtTab, D2D1::RectF(nx, 0, nx + newBtnW, tabH), m_tabTxtBrush);
    m_tabHits.push_back({ nx, 0, newBtnW, tabH, -1, false });

    m_rt->DrawLine(D2D1::Point2F(0, h - 1.f), D2D1::Point2F(w, h - 1.f), m_hrBrush, 1.f);
}

int Renderer::HitTestTab(float x, float y) const {
    for (auto it = m_tabHits.rbegin(); it != m_tabHits.rend(); ++it)
        if (!it->isClose && x >= it->x && x < it->x + it->w
                         && y >= it->y && y < it->y + it->h)
            return it->idx;
    return -2;
}

bool Renderer::HitTestTabClose(float x, float y, int& outIdx) const {
    for (auto it = m_tabHits.rbegin(); it != m_tabHits.rend(); ++it)
        if (it->isClose && x >= it->x && x < it->x + it->w
                        && y >= it->y && y < it->y + it->h) {
            outIdx = it->idx; return true;
        }
    return false;
}

// ─── text layout ─────────────────────────────────────────────────────────────

IDWriteTextFormat* Renderer::FormatFor(const PaintCtx& ctx) const {
    if (ctx.fmtOverride)       return ctx.fmtOverride;
    if (ctx.headingLevel == 1) return m_fmtH1;
    if (ctx.headingLevel == 2) return m_fmtH2;
    if (ctx.headingLevel >= 3) return m_fmtH3;
    if (ctx.isCode)            return m_fmtCode;
    if (ctx.bold && ctx.italic) return m_fmtBold;   // fallback (no bold-italic format)
    if (ctx.bold)              return m_fmtBold;
    if (ctx.italic)            return m_fmtItalic;
    return m_fmtBody;
}

float Renderer::DrawWrappedText(const std::wstring& text,
                                float x, float y, float maxW,
                                IDWriteTextFormat* fmt,
                                ID2D1SolidColorBrush* brush,
                                bool underline,
                                const std::string& href,
                                float scrollY,
                                float topInset,
                                float lineHeightMul,
                                int   textAlign,
                                bool  dryRun,
                                bool  nowrap)
{
    if (text.empty() || !fmt || !m_dwrite) return y;

    float lineH = fmt->GetFontSize() * lineHeightMul;

    // Measure space width
    float spaceW = fmt->GetFontSize() * 0.28f;
    {
        IDWriteTextLayout* sl = nullptr;
        if (SUCCEEDED(m_dwrite->CreateTextLayout(L" ", 1, fmt, 10000, 10000, &sl))) {
            DWRITE_TEXT_METRICS m{}; sl->GetMetrics(&m);
            if (m.width > 0) spaceW = m.width;
            sl->Release();
        }
    }

    // Collect words and their widths
    struct Word { std::wstring txt; float w; };
    std::vector<Word> words;
    {
        size_t i = 0, n = text.size();
        while (i < n) {
            while (i < n && iswspace(text[i])) i++;
            if (i >= n) break;
            size_t j = i;
            while (j < n && !iswspace(text[j])) j++;
            std::wstring w = text.substr(i, j - i);
            float ww = 0;
            IDWriteTextLayout* lay = nullptr;
            if (SUCCEEDED(m_dwrite->CreateTextLayout(
                    w.c_str(), (UINT32)w.size(), fmt, 10000.f, 10000.f, &lay))) {
                DWRITE_TEXT_METRICS m{}; lay->GetMetrics(&m);
                ww = m.width;
                lay->Release();
            }
            words.push_back({std::move(w), ww});
            i = j;
        }
    }
    if (words.empty()) return y;

    // Greedy line packing (single line if nowrap)
    struct Line { int start, end; float w; };
    std::vector<Line> lines;
    {
        int i = 0;
        while (i < (int)words.size()) {
            float lw = 0; int j = i;
            while (j < (int)words.size()) {
                float add = (j > i ? spaceW : 0.f) + words[j].w;
                if (!nowrap && j > i && lw + add > maxW) break;
                lw += add; j++;
            }
            lines.push_back({i, j, lw});
            i = j;
        }
    }

    // Search query for highlighting
    std::wstring query = m_searchQuery;
    std::transform(query.begin(), query.end(), query.begin(), ::towlower);

    for (auto& line : lines) {
        // Alignment offset
        float lx = x;
        if      (textAlign == 1) lx = x + (maxW - line.w) * 0.5f;  // center
        else if (textAlign == 2) lx = x + maxW - line.w;            // right

        float cx = lx;
        for (int k = line.start; k < line.end; k++) {
            if (k > line.start) cx += spaceW;
            const Word& word = words[k];
            float sy = y - scrollY + topInset;

            if (!dryRun && m_rt && sy + lineH >= topInset && sy < (float)m_height) {
                // Search highlight
                if (!query.empty() && m_findBrush) {
                    std::wstring wl = word.txt;
                    std::transform(wl.begin(), wl.end(), wl.begin(), ::towlower);
                    if (wl.find(query) != std::wstring::npos)
                        m_rt->FillRectangle(D2D1::RectF(cx - 1.f, sy, cx + word.w + 1.f, sy + lineH), m_findBrush);
                }
                // Draw word
                IDWriteTextLayout* dl = nullptr;
                if (SUCCEEDED(m_dwrite->CreateTextLayout(
                        word.txt.c_str(), (UINT32)word.txt.size(),
                        fmt, word.w + 2.f, lineH * 2.f, &dl))) {
                    if (underline) {
                        DWRITE_TEXT_RANGE all{0, (UINT32)word.txt.size()};
                        dl->SetUnderline(TRUE, all);
                    }
                    m_rt->DrawTextLayout(D2D1::Point2F(cx, sy), dl, brush);
                    dl->Release();
                }
            }
            if (!href.empty() && !dryRun) {
                float sy2 = y - scrollY + topInset;
                m_hits.push_back({cx, sy2, word.w, lineH, href});
            }
            cx += word.w;
        }
        y += lineH;
    }
    return y;
}

// Draw a <pre> block preserving newlines/indentation. Returns new y.
float Renderer::DrawPreBlock(const Node* node, PaintCtx& ctx) {
    // Collect raw text from all descendant text nodes
    std::string raw;
    std::function<void(const Node*)> collect = [&](const Node* n) {
        if (!n) return;
        if (n->type == NodeType::Text) { raw += n->text; return; }
        for (auto& c : n->children) collect(c.get());
        if (n->tagName == "br") raw += '\n';
    };
    collect(node);
    // Strip single leading newline (HTML5 spec)
    if (!raw.empty() && raw[0] == '\n') raw.erase(raw.begin());

    auto* fmt  = m_fmtCode ? m_fmtCode : m_fmtBody;
    float fontSize = fmt ? fmt->GetFontSize() : 13.f * m_zoom;
    float lineH    = fontSize * 1.35f;
    float pad      = 8.f;

    // Background rect (computed by counting lines)
    int lineCount = 1;
    for (char ch : raw) if (ch == '\n') lineCount++;
    float totalH = lineCount * lineH + pad * 2;

    if (!ctx.dryRun && m_rt) {
        float sy = ctx.y - ctx.scrollY + ctx.topInset;
        float ey = sy + totalH;
        if (ey > ctx.topInset && sy < ctx.winH && m_codeBgBrush) {
            float csy = std::max(sy, ctx.topInset);
            float cey = std::min(ey, ctx.winH);
            m_rt->FillRectangle(D2D1::RectF(ctx.x, csy, ctx.x + ctx.contentW, cey), m_codeBgBrush);
            // border
            if (m_hrBrush) m_rt->DrawRectangle(D2D1::RectF(ctx.x, sy, ctx.x + ctx.contentW, ey), m_hrBrush, 1.f);
        }
    }

    ctx.y += pad;
    float lineX = ctx.x + pad;
    float lineMaxW = ctx.contentW - pad * 2;

    // Draw each line
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        float sy = ctx.y - ctx.scrollY + ctx.topInset;
        if (!ctx.dryRun && m_rt && fmt && sy + lineH >= ctx.topInset && sy < ctx.winH) {
            std::wstring wl = ToWide(line);
            if (!wl.empty()) {
                // Mono text: draw directly (preserves spaces)
                m_rt->DrawText(wl.c_str(), (UINT32)wl.size(), fmt,
                    D2D1::RectF(lineX, sy, lineX + lineMaxW, sy + lineH),
                    m_textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        ctx.y += lineH;
    }
    ctx.y += pad;
    return ctx.y;
}

// ─── stylesheet helpers ───────────────────────────────────────────────────────

Stylesheet Renderer::CollectStylesheet(const Node* root) {
    Stylesheet sheet;
    std::function<void(const Node*)> walk = [&](const Node* n) {
        if (!n) return;
        if (n->type == NodeType::Element && n->tagName == "style") {
            std::string css;
            for (auto& c : n->children)
                if (c->type == NodeType::Text) css += c->text;
            Stylesheet part = ParseStylesheet(css);
            for (auto& r : part.rules) sheet.rules.push_back(r);
        }
        for (auto& c : n->children) walk(c.get());
    };
    walk(root);
    return sheet;
}

CssColor Renderer::FindBodyBgColor(const Node* root, const Stylesheet& sheet) {
    if (!root) return {};
    std::function<CssColor(const Node*)> find = [&](const Node* n) -> CssColor {
        if (!n) return {};
        if (n->type == NodeType::Element
            && (n->tagName == "body" || n->tagName == "html")) {
            auto cs = sheet.resolve(n);
            if (cs.bgColor.valid) return cs.bgColor;
        }
        for (auto& c : n->children) {
            auto bg = find(c.get());
            if (bg.valid) return bg;
        }
        return {};
    };
    return find(root);
}

// ─── DOM walker ───────────────────────────────────────────────────────────────

float Renderer::WalkNode(const Node* node, PaintCtx& ctx) {
    if (!node) return ctx.y;

    // ── text node ─────────────────────────────────────────────────────────
    if (node->type == NodeType::Text) {
        if (node->text.empty()) return ctx.y;

        // Normalize: UTF-8 non-breaking space (0xC2 0xA0) → regular space
        std::string normalized;
        normalized.reserve(node->text.size());
        for (size_t i = 0; i < node->text.size(); ) {
            unsigned char c0 = (unsigned char)node->text[i];
            if (c0 == 0xC2 && i + 1 < node->text.size() && (unsigned char)node->text[i+1] == 0xA0) {
                normalized += ' '; i += 2;
            } else {
                normalized += node->text[i++];
            }
        }

        std::wstring wtext = ToWide(normalized);

        // Apply text-transform
        if (ctx.textTransform == 1) {
            for (auto& c : wtext) c = (wchar_t)towupper(c);
        } else if (ctx.textTransform == 2) {
            for (auto& c : wtext) c = (wchar_t)towlower(c);
        } else if (ctx.textTransform == 3) {
            bool capNext = true;
            for (auto& c : wtext) {
                if (iswspace(c)) { capNext = true; }
                else if (capNext) { c = (wchar_t)towupper(c); capNext = false; }
            }
        }

        auto* fmt   = FormatFor(ctx);
        auto* brush = ctx.colorOverride ? ctx.colorOverride
                    : (ctx.isLink ? m_linkBrush : m_textBrush);
        ctx.y = DrawWrappedText(
            wtext, ctx.x, ctx.y, ctx.contentW,
            fmt, brush, ctx.isLink,
            ctx.isLink ? ctx.linkHref : "",
            ctx.scrollY, ctx.topInset, ctx.lineHeightMul,
            ctx.textAlign, ctx.dryRun, ctx.whiteSpaceNowrap);
        return ctx.y;
    }

    const std::string& tag = node->tagName;

    // ── CSS cascade ───────────────────────────────────────────────────────
    ComputedStyle cs;
    if (ctx.sheet) cs = ctx.sheet->resolve(node);
    if (cs.displayNone) return ctx.y;

    // Save & apply inherited style properties
    auto* prevColor   = ctx.colorOverride;
    auto* prevFmt     = ctx.fmtOverride;
    bool  prevBold    = ctx.bold;
    bool  prevItalic  = ctx.italic;
    float prevX       = ctx.x;
    float prevW       = ctx.contentW;
    float prevLH      = ctx.lineHeightMul;
    int   prevAlign   = ctx.textAlign;
    int   prevTT      = ctx.textTransform;
    bool  prevNowrap  = ctx.whiteSpaceNowrap;
    std::string prevFamily = ctx.fontFamily;

    if (cs.color.valid)       ctx.colorOverride = ctx.dryRun ? nullptr : TempBrush(ToD2D(cs.color));
    if (cs.boldSet)           ctx.bold      = cs.bold;
    if (cs.italicSet)         ctx.italic    = cs.italic;
    if (cs.textAlignSet)      ctx.textAlign = cs.textAlign;
    if (cs.textTransformSet)  ctx.textTransform = cs.textTransform;
    if (cs.whiteSpaceSet)     ctx.whiteSpaceNowrap = cs.whiteSpaceNowrap;
    if (!cs.fontFamily.empty()) ctx.fontFamily = cs.fontFamily;

    // Rebuild fmt if size or family changed
    bool needFmt = (cs.fontSize > 0) || (!cs.fontFamily.empty() && cs.fontFamily != prevFamily);
    if (needFmt && !ctx.dryRun) {
        float sz = cs.fontSize > 0 ? cs.fontSize : (m_fmtBody ? m_fmtBody->GetFontSize() / m_zoom : 15.f);
        ctx.fmtOverride = TempFormat(sz, ctx.bold, ctx.isCode, ctx.italic, ctx.fontFamily);
    } else if (needFmt) {
        ctx.fmtOverride = nullptr;
    }
    if (cs.lineHeight > 0) {
        float base = cs.fontSize > 0 ? cs.fontSize : 15.f;
        ctx.lineHeightMul = cs.lineHeight / base;
    }

    auto walkChildren = [&]() {
        for (auto& c : node->children) WalkNode(c.get(), ctx);
    };

    // ── inline elements ───────────────────────────────────────────────────
    if (tag == "a") {
        bool was = ctx.isLink; std::string wasHref = ctx.linkHref;
        ctx.isLink   = true;
        ctx.linkHref = ResolveUrl(node->attr("href"), ctx.baseUrl);
        walkChildren();
        ctx.isLink = was; ctx.linkHref = wasHref;
        goto restore;
    }
    if (tag == "strong" || tag == "b") {
        bool was = ctx.bold; ctx.bold = true;
        walkChildren();
        ctx.bold = was;
        goto restore;
    }
    if (tag == "em" || tag == "i" || tag == "cite") {
        bool was = ctx.italic; ctx.italic = true;
        walkChildren();
        ctx.italic = was;
        goto restore;
    }
    if (tag == "code" || tag == "tt" || tag == "kbd" || tag == "samp") {
        bool was = ctx.isCode; ctx.isCode = true;
        walkChildren();
        ctx.isCode = was;
        goto restore;
    }
    if (tag == "span"  || tag == "small" || tag == "abbr" || tag == "time"
     || tag == "mark"  || tag == "label" || tag == "s"    || tag == "del"
     || tag == "ins"   || tag == "u"     || tag == "bdi"  || tag == "bdo") {
        walkChildren(); goto restore;
    }
    if (tag == "br") {
        ctx.y += FormatFor(ctx)->GetFontSize() * ctx.lineHeightMul;
        goto restore;
    }
    if (tag == "img") {
        std::string src = ResolveUrl(node->attr("src"), ctx.baseUrl);
        std::string alt = node->attr("alt");
        auto it = m_images.find(src);
        if (it != m_images.end() && it->second) {
            D2D1_SIZE_F bmpSz = it->second->GetSize();
            float maxW  = cs.width >= 0 ? cs.width * m_zoom : ctx.contentW;
            float scale = std::min(maxW / bmpSz.width, 1.f);
            float dw = bmpSz.width * scale, dh = bmpSz.height * scale;
            if (!ctx.dryRun && m_rt) {
                float sy = ctx.y - ctx.scrollY + ctx.topInset;
                if (sy + dh >= ctx.topInset && sy < ctx.winH)
                    m_rt->DrawBitmap(it->second,
                        D2D1::RectF(ctx.x, sy, ctx.x + dw, sy + dh));
            }
            ctx.y += dh + kMarginY;
        } else {
            if (!src.empty() && !m_loadingImages.count(src)) {
                m_loadingImages.insert(src);
                if (m_imageRequestCb) m_imageRequestCb(src);
            }
            if (!ctx.dryRun) {
                std::wstring ph = ToWide(alt.empty() ? "[image]" : "[" + alt + "]");
                ctx.y = DrawWrappedText(ph, ctx.x, ctx.y, ctx.contentW,
                    m_fmtBody, m_hrBrush, false, "", ctx.scrollY, ctx.topInset,
                    ctx.lineHeightMul, ctx.textAlign, ctx.dryRun);
            }
        }
        goto restore;
    }

    // ── skip non-visual elements ──────────────────────────────────────────
    if (tag == "head"     || tag == "script"   || tag == "style"
     || tag == "noscript" || tag == "svg"      || tag == "canvas"
     || tag == "iframe"   || tag == "template" || tag == "meta"
     || tag == "link"     || tag == "title") {
        goto restore;
    }

    // ── preformatted ──────────────────────────────────────────────────────
    if (tag == "pre") {
        ctx.y += kMarginY;
        DrawPreBlock(node, ctx);
        ctx.y += kMarginY;
        goto restore;
    }

    // ── headings ──────────────────────────────────────────────────────────
    if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
        float topGap = (tag[1] == '1') ? kMarginY * 2.f : kMarginY * 1.5f;
        ctx.y += topGap;
        int was = ctx.headingLevel;
        ctx.headingLevel = tag[1] - '0';
        walkChildren();
        ctx.headingLevel = was;
        ctx.y += kMarginY * 0.5f;
        goto restore;
    }

    // ── hr ────────────────────────────────────────────────────────────────
    if (tag == "hr") {
        ctx.y += kMarginY;
        if (!ctx.dryRun && m_rt) {
            float sy = ctx.y - ctx.scrollY + ctx.topInset;
            if (sy >= ctx.topInset && sy < ctx.winH) {
                auto* b = cs.borderColor.valid ? TempBrush(ToD2D(cs.borderColor)) : m_hrBrush;
                if (b) m_rt->DrawLine(D2D1::Point2F(ctx.x, sy),
                               D2D1::Point2F(ctx.x + ctx.contentW, sy), b, 1.f);
            }
        }
        ctx.y += kMarginY;
        goto restore;
    }

    // ── blockquote ───────────────────────────────────────────────────────
    if (tag == "blockquote") {
        ctx.y += kMarginY;
        float barX   = ctx.x;
        float savedX = ctx.x, savedW = ctx.contentW;
        ctx.x       += 20.f;
        ctx.contentW -= 24.f;
        bool wasItalic = ctx.italic; ctx.italic = true;
        float startY = ctx.y;
        walkChildren();
        float endY = ctx.y;
        ctx.italic = wasItalic;
        ctx.x = savedX; ctx.contentW = savedW;
        // Draw left accent bar
        if (!ctx.dryRun && m_rt && m_quoteBrush) {
            float sy = startY - ctx.scrollY + ctx.topInset;
            float ey = endY   - ctx.scrollY + ctx.topInset;
            sy = std::max(sy, ctx.topInset);
            ey = std::min(ey, ctx.winH);
            if (ey > sy) m_rt->FillRectangle(D2D1::RectF(barX + 2.f, sy, barX + 5.f, ey), m_quoteBrush);
        }
        ctx.y += kMarginY;
        goto restore;
    }

    // ── lists ─────────────────────────────────────────────────────────────
    if (tag == "ul") {
        int  wasStyle = ctx.listStyle;
        ctx.listStyle = 1;
        ctx.x       += 20.f; ctx.contentW -= 20.f;
        walkChildren();
        ctx.x       -= 20.f; ctx.contentW += 20.f;
        ctx.listStyle = wasStyle;
        goto restore;
    }
    if (tag == "ol") {
        int wasStyle = ctx.listStyle, wasCtr = ctx.listCounter;
        ctx.listStyle  = 2;
        ctx.listCounter= 0;
        ctx.x       += 24.f; ctx.contentW -= 24.f;
        walkChildren();
        ctx.x       -= 24.f; ctx.contentW += 24.f;
        ctx.listStyle  = wasStyle;
        ctx.listCounter= wasCtr;
        goto restore;
    }
    if (tag == "li") {
        ctx.y += 2.f;
        if (ctx.listStyle == 2) {
            ctx.listCounter++;
            if (!ctx.dryRun && m_rt && m_fmtBody) {
                float sy = ctx.y - ctx.scrollY + ctx.topInset;
                if (sy >= ctx.topInset && sy < ctx.winH) {
                    std::wstring num = std::to_wstring(ctx.listCounter) + L".";
                    m_rt->DrawText(num.c_str(), (UINT32)num.size(), m_fmtBody,
                        D2D1::RectF(ctx.x - 24.f, sy, ctx.x, sy + 22.f),
                        m_textBrush, D2D1_DRAW_TEXT_OPTIONS_NONE);
                }
            }
        } else {
            if (!ctx.dryRun && m_rt && m_fmtBody) {
                float sy = ctx.y - ctx.scrollY + ctx.topInset;
                if (sy >= ctx.topInset && sy < ctx.winH)
                    m_rt->DrawText(L"•", 1, m_fmtBody,
                        D2D1::RectF(ctx.x - 16.f, sy, ctx.x, sy + 22.f),
                        m_textBrush, D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        }
        walkChildren();
        ctx.y += 2.f;
        goto restore;
    }

    // ── form controls ────────────────────────────────────────────────────
    if (tag == "input" || tag == "textarea" || tag == "select") {
        float ctrlH = (tag == "textarea") ? 60.f * m_zoom : 24.f * m_zoom;
        if (!ctx.dryRun && m_rt) {
            float sy = ctx.y - ctx.scrollY + ctx.topInset;
            if (sy + ctrlH >= ctx.topInset && sy < ctx.winH) {
                auto* bg = TempBrush({1.f, 1.f, 1.f, 1.f});
                if (bg) m_rt->FillRectangle(D2D1::RectF(ctx.x, sy, ctx.x + ctx.contentW, sy + ctrlH), bg);
                if (m_hrBrush) m_rt->DrawRectangle(D2D1::RectF(ctx.x, sy, ctx.x + ctx.contentW, sy + ctrlH), m_hrBrush, 1.f);
                std::string val = node->attr("value");
                if (val.empty()) val = node->attr("placeholder");
                if (!val.empty() && m_fmtBody) {
                    auto* tb = TempBrush({.6f, .6f, .6f, 1.f});
                    if (tb) {
                        std::wstring wv = ToWide(val);
                        m_rt->DrawText(wv.c_str(), (UINT32)wv.size(), m_fmtBody,
                            D2D1::RectF(ctx.x + 4.f, sy + 2.f, ctx.x + ctx.contentW - 4.f, sy + ctrlH - 2.f),
                            tb, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                }
            }
        }
        ctx.y += ctrlH + 6.f * m_zoom;
        goto restore;
    }
    if (tag == "button") {
        float ctrlH = 28.f * m_zoom;
        std::string btxt;
        std::function<void(const Node*)> collectBtn = [&](const Node* n) {
            if (!n) return;
            if (n->type == NodeType::Text) btxt += n->text;
            for (auto& c : n->children) collectBtn(c.get());
        };
        collectBtn(node);
        if (!ctx.dryRun && m_rt) {
            float sy  = ctx.y - ctx.scrollY + ctx.topInset;
            float btnW = std::min(ctx.contentW, 150.f * m_zoom);
            if (sy + ctrlH >= ctx.topInset && sy < ctx.winH) {
                auto* bg = TempBrush({.9f, .9f, .9f, 1.f});
                D2D1_RECT_F rect = D2D1::RectF(ctx.x, sy, ctx.x + btnW, sy + ctrlH);
                if (bg) m_rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 4.f, 4.f), bg);
                if (m_hrBrush) m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, 4.f, 4.f), m_hrBrush, 1.f);
                if (!btxt.empty() && m_fmtBody && m_textBrush) {
                    std::wstring wt = ToWide(btxt);
                    m_rt->DrawText(wt.c_str(), (UINT32)wt.size(), m_fmtBody, rect,
                        m_textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
        }
        ctx.y += ctrlH + 6.f * m_zoom;
        goto restore;
    }

    // ── definition list ──────────────────────────────────────────────────
    if (tag == "dt") {
        ctx.y += 4.f;
        bool wasBold = ctx.bold; ctx.bold = true;
        walkChildren();
        ctx.bold = wasBold;
        goto restore;
    }
    if (tag == "dd") {
        ctx.x       += 24.f; ctx.contentW -= 24.f;
        walkChildren();
        ctx.x       -= 24.f; ctx.contentW += 24.f;
        ctx.y += 2.f;
        goto restore;
    }

    // ── table row (side-by-side cells) ───────────────────────────────────
    if (tag == "tr") {
        std::vector<const Node*> cells;
        for (auto& c : node->children)
            if (c->type == NodeType::Element && (c->tagName == "td" || c->tagName == "th"))
                cells.push_back(c.get());
        if (cells.empty()) { walkChildren(); goto restore; }

        float cols   = (float)cells.size();
        float cellW  = ctx.contentW / cols;
        float startY = ctx.y;
        float maxY   = startY;

        for (int ci = 0; ci < (int)cells.size(); ci++) {
            PaintCtx cellCtx    = ctx;
            cellCtx.x           = ctx.x + ci * cellW + 4.f;
            cellCtx.contentW    = cellW - 8.f;
            cellCtx.y           = startY;
            if (cells[ci]->tagName == "th") cellCtx.bold = true;
            for (auto& c : cells[ci]->children)
                WalkNode(c.get(), cellCtx);
            if (cellCtx.y > maxY) maxY = cellCtx.y;
            if (!ctx.dryRun && m_rt && m_hrBrush && ci < (int)cells.size() - 1) {
                float sx = ctx.x + (ci + 1) * cellW;
                float sy = startY - ctx.scrollY + ctx.topInset;
                float ey = maxY   - ctx.scrollY + ctx.topInset;
                if (ey > sy)
                    m_rt->DrawLine(D2D1::Point2F(sx, sy), D2D1::Point2F(sx, ey), m_hrBrush, 0.5f);
            }
        }
        ctx.y = maxY + 4.f;
        if (!ctx.dryRun && m_rt && m_hrBrush) {
            float sy = ctx.y - 2.f - ctx.scrollY + ctx.topInset;
            if (sy >= ctx.topInset && sy < ctx.winH)
                m_rt->DrawLine(D2D1::Point2F(ctx.x, sy),
                               D2D1::Point2F(ctx.x + ctx.contentW, sy), m_hrBrush, 0.5f);
        }
        goto restore;
    }

    // ── table cells (fallback, when tr not present) ──────────────────────
    if (tag == "td" || tag == "th") {
        bool wasBold = ctx.bold;
        if (tag == "th") ctx.bold = true;
        ctx.x       += 6.f; ctx.contentW -= 12.f;
        walkChildren();
        ctx.x       -= 6.f; ctx.contentW += 12.f;
        ctx.y       += 4.f;
        ctx.bold = wasBold;
        goto restore;
    }

    // ── block elements ────────────────────────────────────────────────────
    {
        bool isBlock = (
            tag == "p"       || tag == "div"     || tag == "section"  || tag == "article"
         || tag == "main"    || tag == "header"  || tag == "aside"    || tag == "footer"
         || tag == "nav"     || tag == "form"    || tag == "fieldset" || tag == "details"
         || tag == "summary" || tag == "figure"  || tag == "figcaption"
         || tag == "dl"      || tag == "dt"      || tag == "dd"
         || tag == "table"   || tag == "tr"      || tag == "tbody"    || tag == "thead"
         || tag == "body"    || tag == "#document" || tag == "html"
        );
        bool notRoot = (tag != "body" && tag != "#document" && tag != "html");

        // Box model (values < 0 = not set; -2 = auto for margins)
        float mTop  = cs.marginTop    >= 0 ? cs.marginTop    * m_zoom
                    : (isBlock && notRoot ? kMarginY * 0.5f : 0.f);
        float mBot  = cs.marginBottom >= 0 ? cs.marginBottom * m_zoom
                    : (isBlock && notRoot ? kMarginY * 0.5f : 0.f);
        float mLeft = (cs.marginLeft >= 0) ? cs.marginLeft * m_zoom : 0.f;
        float pTop  = cs.paddingTop   >= 0 ? cs.paddingTop    * m_zoom : 0.f;
        float pBot  = cs.paddingBottom>= 0 ? cs.paddingBottom * m_zoom : 0.f;
        float pLeft = cs.paddingLeft  >= 0 ? cs.paddingLeft   * m_zoom : 0.f;
        float pRight= cs.paddingRight >= 0 ? cs.paddingRight  * m_zoom : 0.f;
        float bw    = cs.borderWidth  >= 0 ? cs.borderWidth   * m_zoom : 0.f;
        float pw    = cs.width        >= 0 ? cs.width         * m_zoom : -1.f;
        float maxw  = cs.maxWidth     >= 0 ? cs.maxWidth      * m_zoom : -1.f;

        if (isBlock) ctx.y += mTop;
        float boxStartY = ctx.y;

        // Compute inner width
        float innerW = (pw >= 0) ? pw : std::max(10.f, prevW - mLeft - (bw+pLeft) - (bw+pRight));
        if (maxw >= 0 && innerW > maxw) innerW = maxw;

        // Horizontal auto-centering: margin-left==auto && margin-right==auto
        bool autoCenter = (cs.marginLeft <= -1.5f && cs.marginRight <= -1.5f && maxw >= 0 && innerW < prevW);
        if (autoCenter) mLeft = (prevW - innerW - (bw+pLeft) - (bw+pRight)) * 0.5f;

        // Set up inner ctx
        ctx.x        = prevX + mLeft + bw + pLeft;
        ctx.contentW = innerW;
        ctx.y       += bw + pTop;

        // If background is set and we're not in a dry run:
        // do a cheap dry-run pass first to know the box height, draw bg, then real pass.
        if (cs.bgColor.valid && isBlock && !ctx.dryRun) {
            PaintCtx dry = ctx;
            dry.dryRun = true;
            for (auto& c : node->children) WalkNode(c.get(), dry);
            float dryEndY = dry.y + pBot + bw;

            float sy = boxStartY - ctx.scrollY + ctx.topInset;
            float ey = dryEndY   - ctx.scrollY + ctx.topInset;
            if (ey > ctx.topInset && sy < ctx.winH && m_rt) {
                float csy = std::max(sy, ctx.topInset);
                float cey = std::min(ey, ctx.winH);
                if (cey > csy) {
                    auto* bg = TempBrush(ToD2D(cs.bgColor));
                    if (bg) {
                        D2D1_RECT_F rect = D2D1::RectF(prevX + mLeft, sy,
                                                        prevX + mLeft + innerW + (bw+pLeft) + (bw+pRight), ey);
                        if (cs.borderRadius > 0)
                            m_rt->FillRoundedRectangle(
                                D2D1::RoundedRect(rect, cs.borderRadius * m_zoom, cs.borderRadius * m_zoom), bg);
                        else
                            m_rt->FillRectangle(rect, bg);
                    }
                }
            }
        }

        // Real children pass
        walkChildren();
        ctx.y += pBot + bw;
        float boxEndY = ctx.y;

        // Border
        if (bw > 0 && !ctx.dryRun && m_rt) {
            float sy = boxStartY - ctx.scrollY + ctx.topInset;
            float ey = boxEndY   - ctx.scrollY + ctx.topInset;
            if (ey > ctx.topInset && sy < ctx.winH) {
                auto* bc = cs.borderColor.valid ? TempBrush(ToD2D(cs.borderColor)) : m_hrBrush;
                if (bc) {
                    D2D1_RECT_F rect = D2D1::RectF(prevX + mLeft, sy,
                                                    prevX + mLeft + innerW + (bw+pLeft) + (bw+pRight), ey);
                    if (cs.borderRadius > 0)
                        m_rt->DrawRoundedRectangle(
                            D2D1::RoundedRect(rect, cs.borderRadius * m_zoom, cs.borderRadius * m_zoom), bc, bw);
                    else
                        m_rt->DrawRectangle(rect, bc, bw);
                }
            }
        }

        if (isBlock) ctx.y += mBot;
    }

restore:
    ctx.colorOverride    = prevColor;
    ctx.fmtOverride      = prevFmt;
    ctx.bold             = prevBold;
    ctx.italic           = prevItalic;
    ctx.x                = prevX;
    ctx.contentW         = prevW;
    ctx.lineHeightMul    = prevLH;
    ctx.textAlign        = prevAlign;
    ctx.textTransform    = prevTT;
    ctx.whiteSpaceNowrap = prevNowrap;
    ctx.fontFamily       = prevFamily;
    return ctx.y;
}

// ─── public paint ─────────────────────────────────────────────────────────────

float Renderer::Paint(const std::shared_ptr<Node>& doc,
                      float scrollY,
                      const std::string& baseUrl,
                      float topInset,
                      float tabStripH,
                      const std::vector<TabEntry>* tabs)
{
    if (!EnsureTarget()) return 0.f;

    for (auto* b : m_tempBrushes) if (b) b->Release();
    m_tempBrushes.clear();
    for (auto* f : m_tempFormats) if (f) f->Release();
    m_tempFormats.clear();
    m_hits.clear();

    m_rt->BeginDraw();

    Stylesheet sheet;
    CssColor   pageBg;
    if (doc) {
        sheet  = CollectStylesheet(doc.get());
        pageBg = FindBodyBgColor(doc.get(), sheet);
    }

    D2D1_COLOR_F bgF = pageBg.valid ? ToD2D(pageBg) : D2D1::ColorF(0.99f, 0.99f, 0.99f);
    m_rt->Clear(bgF);

    // Chrome area (toolbar + tab strip)
    if (topInset > 0)
        m_rt->FillRectangle(D2D1::RectF(0, 0, (float)m_width, topInset), m_bgBrush);

    if (tabs && !tabs->empty() && tabStripH > 0)
        DrawTabStrip(*tabs, tabStripH);

    if (doc) {
        PaintCtx ctx;
        ctx.y        = 16.f;
        ctx.x        = kMarginX;
        ctx.contentW = std::max(100.f, (float)m_width - kMarginX * 2.f);
        ctx.scrollY  = scrollY;
        ctx.winH     = (float)m_height;
        ctx.topInset = topInset;
        ctx.baseUrl  = baseUrl;
        ctx.sheet    = &sheet;
        WalkNode(doc.get(), ctx);

        HRESULT hr = m_rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) ReleaseTarget();
        return ctx.y + 32.f;
    }

    m_rt->EndDraw();
    return 0.f;
}

std::string Renderer::HitTest(float x, float y) const {
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
        if (x >= it->x && x <= it->x + it->w
         && y >= it->y && y <= it->y + it->h)
            return it->href;
    return {};
}
