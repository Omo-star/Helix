#include "css/stylesheet.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>

// ─── length parsing ──────────────────────────────────────────────────────────

// Parse a CSS length into pixels.  Returns -1 for inherit/auto/none/unknown.
static float ParseLength(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
    if (s.empty()) return -1;
    std::string low;
    for (char c : s) low += (char)std::tolower((unsigned char)c);
    if (low == "inherit" || low == "initial" || low == "unset" || low == "normal") return -1;
    if (low == "none" || low == "auto") return -1;
    if (low == "0") return 0;
    // parse numeric part
    size_t i = 0;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) i++;
    size_t numStart = i;
    bool dot = false;
    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || (s[i] == '.' && !dot))) {
        if (s[i] == '.') dot = true;
        i++;
    }
    if (i == numStart) return 0;
    float num = 0;
    try { num = std::stof(s.substr(0, i)); } catch (...) { return 0; }
    std::string unit = low.substr(i);
    while (!unit.empty() && unit[0] == ' ') unit.erase(unit.begin());
    if (unit.empty() || unit == "px") return num;
    if (unit == "em" || unit == "rem") return num * 16.f;  // approx: 1em ≈ 16px
    if (unit == "pt")  return num * 1.333f;
    if (unit == "pc")  return num * 16.f;
    if (unit == "ex" || unit == "ch") return num * 8.f;
    if (unit == "%")   return num * 0.16f;  // rough: 100% ≈ 16px base
    if (unit == "vw" || unit == "vh") return num * 8.f; // rough viewport
    return num;  // unrecognized unit, treat as px
}

// ─── string helpers ──────────────────────────────────────────────────────────

static std::string sLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static std::string sTrim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
    return s;
}
static std::string stripQuotes(std::string s) {
    s = sTrim(s);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"')
                       || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// ─── color parsing ───────────────────────────────────────────────────────────

static const std::map<std::string,CssColor>& namedColors() {
    static std::map<std::string,CssColor> m = {
        #define C(n,r,g,b) {n,{true,r,g,b,1}}
        C("black",0,0,0),         C("white",1,1,1),
        C("red",1,0,0),           C("green",0,.502f,0),
        C("blue",0,0,1),          C("yellow",1,1,0),
        C("orange",1,.647f,0),    C("purple",.502f,0,.502f),
        C("pink",1,.753f,.796f),  C("cyan",0,1,1),
        C("magenta",1,0,1),       C("gray",.502f,.502f,.502f),
        C("grey",.502f,.502f,.502f), C("silver",.753f,.753f,.753f),
        C("teal",0,.502f,.502f),  C("navy",0,0,.502f),
        C("maroon",.502f,0,0),    C("olive",.502f,.502f,0),
        C("lime",0,1,0),          C("aqua",0,1,1),
        C("fuchsia",1,0,1),       C("brown",.647f,.165f,.165f),
        C("coral",1,.498f,.314f), C("crimson",.863f,.078f,.235f),
        C("darkblue",0,0,.545f),  C("darkgreen",0,.392f,0),
        C("darkred",.545f,0,0),   C("gold",1,.843f,0),
        C("hotpink",1,.412f,.706f),C("indigo",.294f,0,.510f),
        C("lavender",.902f,.902f,.980f),
        C("lightblue",.678f,.847f,.902f),
        C("lightgray",.827f,.827f,.827f),
        C("lightgrey",.827f,.827f,.827f),
        C("lightgreen",.565f,.933f,.565f),
        C("salmon",.980f,.502f,.447f),
        C("skyblue",.529f,.808f,.922f),
        C("tomato",1,.388f,.278f),
        C("transparent",0,0,0),
        C("violet",.933f,.510f,.933f),
        C("deepskyblue",0,.749f,1),
        C("dimgray",.412f,.412f,.412f),
        C("dimgrey",.412f,.412f,.412f),
        C("wheat",.961f,.871f,.702f),
        C("tan",.824f,.706f,.549f),
        C("khaki",.941f,.902f,.549f),
        #undef C
    };
    return m;
}

CssColor ParseCssColor(const std::string& raw) {
    std::string s = sLower(sTrim(raw));
    CssColor out;
    if (s.empty() || s == "inherit" || s == "initial") return out;

    auto& nm = namedColors();
    auto it = nm.find(s);
    if (it != nm.end()) return it->second;

    if (!s.empty() && s[0] == '#') {
        std::string h = s.substr(1);
        if (h.size() == 3 || h.size() == 4) {
            std::string e; for (char c : h) e += {c,c}; h = e;
        }
        if (h.size() >= 6) {
            try {
                out.r = std::stoi(h.substr(0,2),nullptr,16)/255.f;
                out.g = std::stoi(h.substr(2,2),nullptr,16)/255.f;
                out.b = std::stoi(h.substr(4,2),nullptr,16)/255.f;
                out.a = h.size()>=8 ? std::stoi(h.substr(6,2),nullptr,16)/255.f : 1.f;
                out.valid = true;
            } catch (...) {}
        }
        return out;
    }

    if (s.substr(0,4) == "rgb(") {
        size_t end = s.find(')');
        if (end != std::string::npos) {
            std::istringstream ss(s.substr(4, end-4));
            std::string tok; std::vector<float> v;
            while (std::getline(ss, tok, ','))
                try { v.push_back(std::stof(sTrim(tok))); } catch (...) {}
            if (v.size() >= 3) {
                out.r=v[0]/255.f; out.g=v[1]/255.f; out.b=v[2]/255.f;
                out.a = v.size()>=4 ? v[3] : 1.f;
                out.valid = true;
            }
        }
    }
    return out;
}

// ─── declaration parsing (shared between inline and stylesheet) ───────────────

static void ApplyDeclaration(const std::string& prop,
                             const std::string& val,
                             ComputedStyle& out) {
    if (prop == "color") {
        out.color = ParseCssColor(val);
    } else if (prop == "background-color") {
        out.bgColor = ParseCssColor(val);
    } else if (prop == "background") {
        // background shorthand: try as color first, then look for url()
        std::string low = sLower(val);
        if (low.find("url(") != std::string::npos) {
            size_t us = low.find("url("), ue = val.find(')', us + 4);
            if (ue != std::string::npos) {
                std::string url = sTrim(val.substr(us + 4, ue - us - 4));
                out.backgroundImage = stripQuotes(url);
            }
        } else {
            CssColor c = ParseCssColor(val);
            if (c.valid) out.bgColor = c;
        }
    } else if (prop == "background-image") {
        std::string low = sLower(val);
        if (low.find("url(") != std::string::npos) {
            size_t us = low.find("url("), ue = val.find(')', us + 4);
            if (ue != std::string::npos) {
                std::string url = sTrim(val.substr(us + 4, ue - us - 4));
                out.backgroundImage = stripQuotes(url);
            }
        }
    } else if (prop == "font-size") {
        std::string v = sLower(sTrim(val));
        if      (v == "small")    out.fontSize = 12;
        else if (v == "medium")   out.fontSize = 15;
        else if (v == "large")    out.fontSize = 18;
        else if (v == "x-large")  out.fontSize = 22;
        else if (v == "xx-large") out.fontSize = 28;
        else if (v == "smaller")  out.fontSize = 12;
        else if (v == "larger")   out.fontSize = 18;
        else { float f = ParseLength(val); if (f > 0) out.fontSize = f; }
    } else if (prop == "font-weight") {
        std::string v = sLower(val);
        out.boldSet = true;
        int w = 0; try { w = std::stoi(v); } catch (...) {}
        out.bold = (v == "bold" || v == "bolder" || w >= 600);
    } else if (prop == "font-style") {
        out.italicSet = true;
        out.italic = (sLower(val) == "italic" || sLower(val) == "oblique");
    } else if (prop == "font-family") {
        std::string v = val;
        size_t comma = v.find(',');
        if (comma != std::string::npos) v = v.substr(0, comma);
        v = stripQuotes(sTrim(v));
        std::string low = sLower(v);
        if      (low == "sans-serif")                           v = "Segoe UI";
        else if (low == "serif")                                v = "Georgia";
        else if (low == "monospace" || low == "courier"
              || low == "courier new")                          v = "Consolas";
        else if (low == "helvetica" || low == "arial")          v = "Arial";
        else if (low == "times" || low == "times new roman")    v = "Times New Roman";
        else if (low == "system-ui" || low == "ui-sans-serif")  v = "Segoe UI";
        out.fontFamily = v;
    } else if (prop == "font") {
        // simplified font shorthand: extract weight, style, size, family
        std::istringstream vs(val); std::string tok;
        while (vs >> tok) {
            std::string low = sLower(tok);
            if (low == "bold" || low == "bolder") { out.bold = true; out.boldSet = true; }
            else if (low == "italic" || low == "oblique") { out.italic = true; out.italicSet = true; }
            else if (low == "normal") { /* no-op */ }
            else {
                // Check for size (maybe size/lineheight)
                size_t slash = low.find('/');
                std::string sizePart = (slash != std::string::npos) ? low.substr(0, slash) : low;
                float sz = ParseLength(sizePart);
                if (sz > 0) {
                    out.fontSize = sz;
                    if (slash != std::string::npos) {
                        float lh = 0;
                        try { lh = std::stof(low.substr(slash + 1)); } catch (...) {}
                        if (lh > 0) out.lineHeight = lh * sz;
                    }
                    // rest of string is font family
                    std::string rest;
                    while (vs >> tok) { if (!rest.empty()) rest += " "; rest += tok; }
                    if (!rest.empty()) {
                        ComputedStyle tmp; ApplyDeclaration("font-family", rest, tmp);
                        out.fontFamily = tmp.fontFamily;
                    }
                    break;
                }
            }
        }
    } else if (prop == "text-decoration" || prop == "text-decoration-line") {
        out.underline = (sLower(val).find("underline") != std::string::npos);
    } else if (prop == "text-transform") {
        std::string v = sLower(val);
        out.textTransformSet = true;
        if      (v == "uppercase")  out.textTransform = 1;
        else if (v == "lowercase")  out.textTransform = 2;
        else if (v == "capitalize") out.textTransform = 3;
        else                        out.textTransform = 0;
    } else if (prop == "white-space") {
        std::string v = sLower(val);
        out.whiteSpaceSet = true;
        out.whiteSpaceNowrap = (v == "nowrap" || v == "pre-line");
    } else if (prop == "display") {
        std::string v = sLower(val);
        out.displayNone = (v == "none");
        out.displayFlex = (v == "flex" || v == "inline-flex" || v == "grid" || v == "inline-grid");
    } else if (prop == "margin") {
        std::istringstream vs(val); std::vector<float> v;
        std::string tok;
        while (vs >> tok) {
            if (sLower(tok) == "auto") v.push_back(-2.f);
            else { float f = ParseLength(tok); v.push_back(f < 0 ? 0 : f); }
        }
        if (v.size() == 1) {
            out.marginTop = out.marginRight = out.marginBottom = out.marginLeft = v[0];
        } else if (v.size() == 2) {
            out.marginTop = out.marginBottom = v[0];
            out.marginRight = out.marginLeft = v[1];
        } else if (v.size() == 3) {
            out.marginTop = v[0]; out.marginRight = out.marginLeft = v[1]; out.marginBottom = v[2];
        } else if (v.size() >= 4) {
            out.marginTop = v[0]; out.marginRight = v[1]; out.marginBottom = v[2]; out.marginLeft = v[3];
        }
    } else if (prop == "margin-top") {
        if (sLower(val) == "auto") out.marginTop = -2.f;
        else { float f = ParseLength(val); if (f >= 0) out.marginTop = f; }
    } else if (prop == "margin-bottom") {
        if (sLower(val) == "auto") out.marginBottom = -2.f;
        else { float f = ParseLength(val); if (f >= 0) out.marginBottom = f; }
    } else if (prop == "margin-right") {
        if (sLower(val) == "auto") out.marginRight = -2.f;
        else { float f = ParseLength(val); if (f >= 0) out.marginRight = f; }
    } else if (prop == "margin-left") {
        if (sLower(val) == "auto") out.marginLeft = -2.f;
        else { float f = ParseLength(val); if (f >= 0) out.marginLeft = f; }

    } else if (prop == "padding") {
        std::istringstream vs(val); std::vector<float> v;
        std::string tok;
        while (vs >> tok) { float f = ParseLength(tok); v.push_back(f < 0 ? 0 : f); }
        if (v.size() == 1) {
            out.paddingTop = out.paddingRight = out.paddingBottom = out.paddingLeft = v[0];
        } else if (v.size() == 2) {
            out.paddingTop = out.paddingBottom = v[0];
            out.paddingRight = out.paddingLeft = v[1];
        } else if (v.size() == 3) {
            out.paddingTop = v[0]; out.paddingRight = out.paddingLeft = v[1]; out.paddingBottom = v[2];
        } else if (v.size() >= 4) {
            out.paddingTop = v[0]; out.paddingRight = v[1]; out.paddingBottom = v[2]; out.paddingLeft = v[3];
        }
    } else if (prop == "padding-top") {
        float f = ParseLength(val); if (f >= 0) out.paddingTop    = f;
    } else if (prop == "padding-right") {
        float f = ParseLength(val); if (f >= 0) out.paddingRight  = f;
    } else if (prop == "padding-bottom") {
        float f = ParseLength(val); if (f >= 0) out.paddingBottom = f;
    } else if (prop == "padding-left") {
        float f = ParseLength(val); if (f >= 0) out.paddingLeft   = f;

    } else if (prop == "border-width") {
        float f = ParseLength(val); if (f >= 0) out.borderWidth = f;
    } else if (prop == "border-color") {
        out.borderColor = ParseCssColor(val);
    } else if (prop == "border-radius") {
        float f = ParseLength(val); if (f >= 0) out.borderRadius = f;
    } else if (prop == "border") {
        if (sLower(val) == "none" || sLower(val) == "0") { out.borderWidth = 0; }
        else {
            std::istringstream vs(val); std::string tok;
            while (vs >> tok) {
                float f = ParseLength(tok);
                if (f > 0) { out.borderWidth = f; }
                else if (tok != "solid" && tok != "dashed" && tok != "dotted" && tok != "none") {
                    CssColor c = ParseCssColor(tok);
                    if (c.valid) out.borderColor = c;
                }
            }
        }
    } else if (prop == "border-top" || prop == "border-bottom"
            || prop == "border-left" || prop == "border-right"
            || prop == "border-top-width" || prop == "border-bottom-width"
            || prop == "border-left-width" || prop == "border-right-width") {
        std::istringstream vs(val); std::string tok;
        while (vs >> tok) { float f = ParseLength(tok); if (f >= 0) { out.borderWidth = f; break; } }

    } else if (prop == "line-height") {
        std::string v = sLower(sTrim(val));
        if (v == "normal") out.lineHeight = 0;
        else {
            // Could be unitless multiplier like "1.5" or "1.5em"
            float f = ParseLength(val);
            if (f > 0) out.lineHeight = f;
            else { try { float n = std::stof(val); if (n > 0) out.lineHeight = n * 16.f; } catch (...) {} }
        }
    } else if (prop == "width") {
        float f = ParseLength(val); if (f >= 0) out.width = f;
    } else if (prop == "height") {
        float f = ParseLength(val); if (f >= 0) out.height = f;
    } else if (prop == "max-width") {
        float f = ParseLength(val); if (f >= 0) out.maxWidth = f;
    } else if (prop == "min-width") {
        float f = ParseLength(val); if (f >= 0) out.minWidth = f;
    } else if (prop == "min-height") {
        float f = ParseLength(val); if (f >= 0) out.minHeight = f;
    } else if (prop == "text-align") {
        std::string v = sLower(sTrim(val));
        out.textAlignSet = true;
        if      (v == "center") out.textAlign = 1;
        else if (v == "right")  out.textAlign = 2;
        else                    out.textAlign = 0;
    } else if (prop == "opacity") {
        // not stored separately — could multiply into color alpha
    }
}

ComputedStyle ParseInlineStyle(const std::string& style) {
    ComputedStyle out;
    std::istringstream ss(style);
    std::string decl;
    while (std::getline(ss, decl, ';')) {
        size_t colon = decl.find(':');
        if (colon == std::string::npos) continue;
        ApplyDeclaration(sLower(sTrim(decl.substr(0, colon))),
                         sTrim(decl.substr(colon+1)), out);
    }
    return out;
}

// ─── selector matching ───────────────────────────────────────────────────────

static bool MatchesSimpleSelector(const CssSelectorPart& part, const Node* node) {
    if (!node || node->type != NodeType::Element) return false;
    if (!part.tag.empty() && node->tagName != part.tag) return false;
    if (!part.id.empty() && node->attr("id") != part.id) return false;
    if (!part.attrName.empty()) {
        std::string value = node->attr(part.attrName);
        if (value.empty() && node->attrs.find(part.attrName) == node->attrs.end()) return false;
        if (part.attrHasValue && value != part.attrValue) return false;
    }
    if (!part.cls.empty()) {
        auto ca = node->attr("class");
        bool found = false;
        std::istringstream ss(ca);
        std::string tok;
        while (ss >> tok) if (tok == part.cls) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

static const Node* PreviousElementSibling(const Node* node) {
    if (!node || !node->parent) return nullptr;
    const Node* previous = nullptr;
    for (const auto& child : node->parent->children) {
        if (child.get() == node) return previous;
        if (child->type == NodeType::Element) previous = child.get();
    }
    return nullptr;
}

int CssRule::specificity() const {
    if (selector.empty()) {
        return (!id.empty() ? 100 : 0)
             + (!cls.empty() ?  10 : 0)
             + (!tag.empty() ?   1 : 0);
    }

    int total = 0;
    for (const auto& part : selector) {
        total += (!part.id.empty() ? 100 : 0)
               + (!part.cls.empty() ? 10 : 0)
               + (!part.attrName.empty() ? 10 : 0)
               + (!part.tag.empty() ? 1 : 0);
    }
    return total;
}

bool CssRule::matches(const Node* node) const {
    if (!node || node->type != NodeType::Element) return false;
    if (!selector.empty()) {
        int i = (int)selector.size() - 1;
        const Node* current = node;
        if (!MatchesSimpleSelector(selector[i], current)) return false;

        for (; i > 0; --i) {
            char combinator = selector[i].combinator;
            const CssSelectorPart& wanted = selector[i - 1];

            if (combinator == '>') {
                current = current->parent;
                if (!MatchesSimpleSelector(wanted, current)) return false;
            } else if (combinator == '+') {
                current = PreviousElementSibling(current);
                if (!MatchesSimpleSelector(wanted, current)) return false;
            } else {
                const Node* ancestor = current->parent;
                bool found = false;
                while (ancestor) {
                    if (MatchesSimpleSelector(wanted, ancestor)) {
                        current = ancestor;
                        found = true;
                        break;
                    }
                    ancestor = ancestor->parent;
                }
                if (!found) return false;
            }
        }
        return true;
    }

    if (!tag.empty() && node->tagName != tag) return false;
    if (!id.empty()  && node->attr("id") != id) return false;
    if (!cls.empty()) {
        // class attr is space-separated list
        auto ca = node->attr("class");
        bool found = false;
        std::istringstream ss(ca);
        std::string tok;
        while (ss >> tok) if (tok == cls) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

// ─── cascade resolution ──────────────────────────────────────────────────────

ComputedStyle Stylesheet::resolve(const Node* node) const {
    // Collect matching rules, sorted by specificity
    std::vector<const CssRule*> matched;
    for (auto& r : rules) if (r.matches(node)) matched.push_back(&r);
    std::stable_sort(matched.begin(), matched.end(),
        [](const CssRule* a, const CssRule* b) {
            return a->specificity() < b->specificity();
        });

    ComputedStyle out;
    for (auto* r : matched) out = out.inherit(r->style);

    // Inline style wins over everything
    auto inl = node->attr("style");
    if (!inl.empty()) out = out.inherit(ParseInlineStyle(inl));
    return out;
}

// ─── CSS text parser ─────────────────────────────────────────────────────────

// Strip /* ... */ comments
static std::string stripComments(const std::string& css) {
    std::string out; out.reserve(css.size());
    size_t i = 0;
    while (i < css.size()) {
        if (i+1 < css.size() && css[i]=='/' && css[i+1]=='*') {
            i += 2;
            while (i+1 < css.size() && !(css[i]=='*' && css[i+1]=='/')) i++;
            i += 2;
        } else {
            out += css[i++];
        }
    }
    return out;
}

// Parse one simple selector like "div", ".foo", "#bar", "div.foo", "a#id"
static CssSelectorPart parseSimpleSelectorPart(const std::string& sel) {
    CssSelectorPart part;
    std::string cur;
    char mode = 't'; // t=tag, c=class, i=id
    auto flush = [&]() {
        if (cur.empty()) return;
        if (mode == 't') part.tag = sLower(cur);
        if (mode == 'c') part.cls = cur;
        if (mode == 'i') part.id  = cur;
        cur.clear();
    };
    for (size_t i = 0; i < sel.size(); ++i) {
        char c = sel[i];
        if (c == '.') { flush(); mode = 'c'; }
        else if (c == '#') { flush(); mode = 'i'; }
        else if (c == '[') {
            flush();
            size_t end = sel.find(']', i + 1);
            if (end == std::string::npos) break;
            std::string body = sTrim(sel.substr(i + 1, end - i - 1));
            size_t eq = body.find('=');
            if (eq == std::string::npos) {
                part.attrName = sLower(body);
                part.attrHasValue = false;
            } else {
                part.attrName = sLower(sTrim(body.substr(0, eq)));
                part.attrValue = stripQuotes(body.substr(eq + 1));
                part.attrHasValue = true;
            }
            i = end;
        }
        else if (c == ':') break; // skip pseudo-classes
        else cur += c;
    }
    flush();
    // "*" or empty tag = universal
    if (part.tag == "*") part.tag.clear();
    return part;
}

static std::vector<CssSelectorPart> parseSelectorChain(std::string selector) {
    std::string spaced;
    spaced.reserve(selector.size() + 4);
    for (char c : selector) {
        if (c == '>' || c == '+') {
            spaced += ' ';
            spaced += c;
            spaced += ' ';
        }
        else spaced += c;
    }

    std::vector<CssSelectorPart> parts;
    std::istringstream ss(spaced);
    std::string tok;
    char nextCombinator = 0;
    while (ss >> tok) {
        if (tok == ">" || tok == "+") {
            nextCombinator = tok[0];
            continue;
        }
        CssSelectorPart part = parseSimpleSelectorPart(tok);
        part.combinator = parts.empty() ? 0 : (nextCombinator ? nextCombinator : ' ');
        parts.push_back(part);
        nextCombinator = ' ';
    }
    return parts;
}

static CssRule parseSelector(const std::string& sel) {
    CssRule rule;
    rule.selector = parseSelectorChain(sel);
    if (!rule.selector.empty()) {
        const auto& last = rule.selector.back();
        rule.tag = last.tag;
        rule.cls = last.cls;
        rule.id = last.id;
    }
    return rule;
}

Stylesheet ParseStylesheet(const std::string& rawCss) {
    Stylesheet sheet;
    std::string css = stripComments(rawCss);

    size_t pos = 0;
    while (pos < css.size()) {
        // Find next '{'
        size_t lbrace = css.find('{', pos);
        if (lbrace == std::string::npos) break;
        size_t rbrace = css.find('}', lbrace);
        if (rbrace == std::string::npos) break;

        std::string selectorBlock = sTrim(css.substr(pos, lbrace - pos));
        std::string declBlock     = css.substr(lbrace+1, rbrace - lbrace - 1);
        pos = rbrace + 1;

        if (selectorBlock.empty()) continue;

        // Parse declarations once
        ComputedStyle declStyle;
        std::istringstream ds(declBlock);
        std::string decl;
        while (std::getline(ds, decl, ';')) {
            size_t colon = decl.find(':');
            if (colon == std::string::npos) continue;
            ApplyDeclaration(sLower(sTrim(decl.substr(0, colon))),
                             sTrim(decl.substr(colon+1)), declStyle);
        }

        // Each comma-separated selector becomes its own rule
        std::istringstream ss(selectorBlock);
        std::string selPart;
        while (std::getline(ss, selPart, ',')) {
            selPart = sTrim(selPart);
            if (selPart.empty()) continue;

            CssRule rule = parseSelector(selPart);
            rule.style = declStyle;
            sheet.rules.push_back(rule);
        }
    }
    return sheet;
}

static std::string BoolText(bool value) {
    return value ? "true" : "false";
}

static void AppendColor(std::ostringstream& out, const char* name, const CssColor& color) {
    if (!color.valid) return;
    out << name << "=" << color.r << "," << color.g << "," << color.b << "," << color.a << " ";
}

std::string SerializeComputedStyle(const ComputedStyle& style) {
    std::ostringstream out;
    AppendColor(out, "color", style.color);
    AppendColor(out, "bg", style.bgColor);
    if (style.fontSize > 0) out << "fontSize=" << style.fontSize << " ";
    if (style.boldSet) out << "bold=" << BoolText(style.bold) << " ";
    if (style.italicSet) out << "italic=" << BoolText(style.italic) << " ";
    if (style.underline) out << "underline=true ";
    if (style.displayNone) out << "display=none ";
    if (style.marginTop    >= 0) out << "marginTop="    << style.marginTop    << " ";
    if (style.marginRight  >= 0) out << "marginRight="  << style.marginRight  << " ";
    if (style.marginBottom >= 0) out << "marginBottom=" << style.marginBottom << " ";
    if (style.marginLeft   >= 0) out << "marginLeft="   << style.marginLeft   << " ";
    if (style.paddingTop   >= 0) out << "paddingTop="   << style.paddingTop   << " ";
    if (style.paddingRight >= 0) out << "paddingRight=" << style.paddingRight << " ";
    if (style.paddingBottom>= 0) out << "paddingBottom="<< style.paddingBottom<< " ";
    if (style.paddingLeft  >= 0) out << "paddingLeft="  << style.paddingLeft  << " ";
    if (style.borderWidth  >= 0) out << "borderWidth="  << style.borderWidth  << " ";
    if (style.borderColor.valid) AppendColor(out, "borderColor", style.borderColor);
    if (style.borderRadius > 0)  out << "borderRadius=" << style.borderRadius << " ";
    if (style.lineHeight   > 0)  out << "lineHeight="   << style.lineHeight   << " ";
    if (style.textAlignSet)      out << "textAlign="    << style.textAlign    << " ";
    if (style.width        >= 0) out << "width="        << style.width        << " ";
    if (style.height       >= 0) out << "height="       << style.height       << " ";
    if (style.maxWidth     >= 0) out << "maxWidth="     << style.maxWidth     << " ";
    out << "\n";
    return out.str();
}

std::string SerializeStylesheet(const Stylesheet& sheet) {
    std::ostringstream out;
    for (const auto& rule : sheet.rules) {
        out << "rule";
        if (!rule.tag.empty()) out << " tag=" << rule.tag;
        if (!rule.cls.empty()) out << " class=" << rule.cls;
        if (!rule.id.empty()) out << " id=" << rule.id;
        out << " specificity=" << rule.specificity() << " ";
        out << SerializeComputedStyle(rule.style);
    }
    return out.str();
}
