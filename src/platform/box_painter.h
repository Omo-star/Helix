#pragma once
//
// box_painter.h — cross-platform box-tree painter.
//
// Paints a LayoutBox tree through IPlatformRenderer, with no direct D2D/CG/Cairo
// calls. Used by all platform shells.
//
#include "platform/platform.h"
#include "layout/box.h"
#include "css/style.h"
#include "network/url.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <algorithm>

struct PaintState {
    IPlatformRenderer* r = nullptr;
    float scrollY = 0;
    float topInset = 0;
    std::string baseUrl;
    std::map<std::string, PlatBitmap>* images = nullptr;
    std::vector<HitRegion>* hits = nullptr;
    std::map<std::string, PlatFont>* fontCache = nullptr;
};

inline PlatColor ToPlatColor(const CssColor& c) { return { c.r, c.g, c.b, c.a }; }

inline PlatFont GetOrCreateFont(PaintState& ps, const FontKey& f) {
    std::string key = std::to_string((int)(f.size * 4))
        + (f.bold ? "b" : "-") + (f.italic ? "i" : "-") + (f.mono ? "m" : "-") + f.family;
    auto it = ps.fontCache->find(key);
    if (it != ps.fontCache->end()) return it->second;
    PlatFont pf = ps.r->CreateFont(f.size, f.bold, f.italic, f.mono, f.family);
    (*ps.fontCache)[key] = pf;
    return pf;
}

// ── paint functions ──────────────────────────────────────────────────────────

inline void PaintBoxDecorations(PaintState& ps, const LayoutBox& box) {
    const ComputedStyle& s = box.style;
    float sx = box.x;
    float sy = box.y - ps.scrollY + ps.topInset;
    float bw = box.borderBoxW();
    float bh = box.borderBoxH();

    if (sy + bh < ps.topInset || sy > (float)ps.r->Height()) return;

    // Background color
    if (s.bgColor.valid && s.bgColor.a > 0.001f) {
        ps.r->FillRect(sx, sy, bw, bh, ToPlatColor(s.bgColor));
    }

    // Background image
    if (!s.backgroundImage.empty() && !(s.bgNoRepeat && s.bgFixed) && ps.images) {
        std::string url = ResolveUrlAgainstBase(s.backgroundImage, ps.baseUrl);
        auto it = ps.images->find(url);
        if (it != ps.images->end() && it->second) {
            int rep = s.bgRepeatSet ? s.bgRepeat : (s.bgNoRepeat ? 3 : 0);
            float tw = bw, th = bh;
            float ox = s.bgPosXPct ? (bw - tw) * (s.bgPosX / 100.f) : s.bgPosX;
            float oy = s.bgPosYPct ? (bh - th) * (s.bgPosY / 100.f) : s.bgPosY;
            bool repX = (rep == 0 || rep == 1);
            bool repY = (rep == 0 || rep == 2);
            if (rep == 3) {
                ps.r->PushClip(sx, sy, bw, bh);
                ps.r->DrawBitmap(it->second, sx + ox, sy + oy, tw, th);
                ps.r->PopClip();
            } else {
                ps.r->DrawBitmapTiled(it->second, sx, sy, bw, bh, tw, th, ox, oy, repX, repY);
            }
        }
    }

    // Replaced image
    if (box.kind == BoxKind::Replaced && !box.replacedUrl.empty() && ps.images) {
        auto it = ps.images->find(box.replacedUrl);
        if (it != ps.images->end() && it->second) {
            float cx = box.contentX();
            float cy = box.contentY() - ps.scrollY + ps.topInset;
            ps.r->DrawBitmap(it->second, cx, cy, box.contentW, box.contentH);
        }
    }

    // Borders
    auto borderColor = [&](const CssColor& side) -> PlatColor {
        if (side.valid) return ToPlatColor(side);
        if (s.borderColor.valid) return ToPlatColor(s.borderColor);
        if (s.color.valid) return ToPlatColor(s.color);
        return { 0.7f, 0.7f, 0.7f, 1.f };
    };
    if (box.borderTop > 0)
        ps.r->FillRect(sx, sy, bw, box.borderTop, borderColor(s.borderTopColor));
    if (box.borderBottom > 0)
        ps.r->FillRect(sx, sy + bh - box.borderBottom, bw, box.borderBottom, borderColor(s.borderBottomColor));
    if (box.borderLeft > 0)
        ps.r->FillRect(sx, sy, box.borderLeft, bh, borderColor(s.borderLeftColor));
    if (box.borderRight > 0)
        ps.r->FillRect(sx + bw - box.borderRight, sy, box.borderRight, bh, borderColor(s.borderRightColor));

    // List marker
    if (box.kind == BoxKind::ListItem) {
        FontKey fk;
        fk.size = std::max(1.f, (s.fontSize > 0 ? s.fontSize : 16.f));
        fk.bold = s.bold;
        PlatFont font = GetOrCreateFont(ps, fk);
        float markerY = box.contentY() - ps.scrollY + ps.topInset;
        PlatColor tc = s.color.valid ? ToPlatColor(s.color) : PlatColor{0,0,0,1};
        ps.r->DrawText(L"•", box.contentX() - 16.f, markerY, 16.f, 24.f, font, tc);
    }
}

inline void PaintLines(PaintState& ps, const LayoutBox& box) {
    for (const auto& line : box.lines) {
        for (const auto& frag : line.frags) {
            if (!frag.src) continue;
            if (frag.src->kind == BoxKind::InlineBlock || frag.src->kind == BoxKind::Replaced) {
                // Paint the atomic box recursively
                extern void PaintBoxTree(PaintState& ps, const LayoutBox& box);
                PaintBoxTree(ps, *frag.src);
                continue;
            }
            const ComputedStyle& fs = frag.src->style;
            if (fs.visibilitySet && fs.visibilityHidden) continue;
            if (fs.opacitySet && fs.opacity < 0.01f) continue;
            float sy = frag.y - ps.scrollY + ps.topInset;
            if (sy + frag.h < ps.topInset || sy > (float)ps.r->Height()) {
                if (ps.hits && !frag.src->href.empty())
                    ps.hits->push_back({ frag.x, sy, frag.w, frag.h, frag.src->href });
                continue;
            }
            FontKey fk;
            fk.size = std::clamp((fs.fontSize > 0 ? fs.fontSize : 16.f), 1.f, 40.f);
            fk.bold = fs.bold; fk.italic = fs.italic; fk.family = fs.fontFamily;
            std::string fl; for (char c : fs.fontFamily) fl += (char)std::tolower((unsigned char)c);
            fk.mono = (fl.find("mono") != std::string::npos || fl.find("consol") != std::string::npos
                    || fl.find("courier") != std::string::npos);
            PlatFont font = GetOrCreateFont(ps, fk);
            PlatColor color = fs.color.valid ? ToPlatColor(fs.color)
                            : (!frag.src->href.empty() ? PlatColor{0.1f,0.1f,0.8f,1} : PlatColor{0,0,0,1});
            bool underline = fs.underline || !frag.src->href.empty();
            ps.r->DrawText(frag.text, frag.x, sy, frag.w + 4.f, frag.h * 2.f + 4.f,
                           font, color, underline);
            if (ps.hits && !frag.src->href.empty())
                ps.hits->push_back({ frag.x, sy, frag.w, frag.h, frag.src->href });
        }
    }
}

inline void PaintBoxTree(PaintState& ps, const LayoutBox& box) {
    static thread_local int depth = 0;
    if (depth > 600) return;
    struct G { int& d; G(int& x):d(x){++d;} ~G(){--d;} } g(depth);
    if (box.style.isDisplayNone()) return;

    bool hidden = (box.style.visibilitySet && box.style.visibilityHidden)
               || (box.style.opacitySet && box.style.opacity < 0.01f);

    // Decorations
    if (!hidden && box.kind != BoxKind::Text && box.kind != BoxKind::Inline && box.kind != BoxKind::Break)
        PaintBoxDecorations(ps, box);

    // overflow:hidden clip
    bool clipped = false;
    if (box.style.overflowHidden && !hidden) {
        float effScroll = box.style.positionMode == 3 ? 0.f : ps.scrollY;
        float cx = box.x, cy = box.y - effScroll + ps.topInset;
        float cw = box.borderBoxW(), ch = box.borderBoxH();
        if (cw > 0 && ch > 0) { ps.r->PushClip(cx, cy, cw, ch); clipped = true; }
    }

    // Children (simplified stacking: in-flow, then floats, then positioned)
    if (box.establishesInline) {
        if (!hidden) PaintLines(ps, box);
    } else {
        for (auto& k : box.kids) {
            if (!k->isOutOfFlow() && !k->isFloat()) PaintBoxTree(ps, *k);
        }
    }
    for (auto& k : box.kids) {
        if (k->isFloat()) PaintBoxTree(ps, *k);
    }
    for (auto& k : box.kids) {
        if (k->isOutOfFlow() || k->style.positionMode == 1) PaintBoxTree(ps, *k);
    }

    if (clipped) ps.r->PopClip();
}
