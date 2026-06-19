#pragma once
#include <string>

struct CssColor {
    bool  valid = false;
    float r = 0, g = 0, b = 0, a = 1;
};
CssColor ParseCssColor(const std::string& s);

struct ComputedStyle {
    CssColor    color;
    CssColor    bgColor;
    std::string fontFamily;          // "" = default
    std::string backgroundImage;     // URL from background-image: url(...)
    float    fontSize     = 0;
    bool     bold         = false;
    bool     boldSet      = false;
    bool     italic       = false;
    bool     italicSet    = false;
    bool     underline    = false;
    bool     displayNone  = false;
    bool     displayBlock = false;
    // Box model (-1 = not set, -2 = auto)
    float    marginTop    = -1;
    float    marginRight  = -1;
    float    marginBottom = -1;
    float    marginLeft   = -1;
    float    paddingTop   = -1;
    float    paddingRight = -1;
    float    paddingBottom= -1;
    float    paddingLeft  = -1;
    float    borderWidth  = -1;
    CssColor borderColor;
    float    borderRadius = 0;
    // Text
    float    lineHeight      = 0;
    int      textAlign       = 0;      // 0=left, 1=center, 2=right
    bool     textAlignSet    = false;
    int      textTransform   = 0;      // 0=none, 1=uppercase, 2=lowercase, 3=capitalize
    bool     textTransformSet= false;
    bool     whiteSpaceNowrap= false;
    bool     whiteSpaceSet   = false;
    // Sizing
    float    width        = -1;
    float    widthPercent = -1;
    float    height       = -1;
    float    maxWidth     = -1;
    float    minWidth     = -1;
    float    minHeight    = -1;
    // Layout
    int      floatMode    = 0;       // 0=none, 1=left, 2=right
    int      clearMode    = 0;       // 0=none, 1=left, 2=right, 3=both
    int      positionMode = 0;       // 0=static, 1=relative, 2=absolute, 3=fixed
    bool     overflowHidden = false;
    float    top          = 0;
    float    right        = 0;
    float    bottom       = 0;
    float    left         = 0;
    bool     topSet       = false;
    bool     rightSet     = false;
    bool     bottomSet    = false;
    bool     leftSet      = false;
    // Flex (very simplified)
    bool     displayFlex  = false;

    ComputedStyle inherit(const ComputedStyle& child) const {
        ComputedStyle out = *this;
        if (child.color.valid)       out.color   = child.color;
        if (child.bgColor.valid)     out.bgColor = child.bgColor;
        if (!child.fontFamily.empty())      out.fontFamily      = child.fontFamily;
        if (!child.backgroundImage.empty()) out.backgroundImage = child.backgroundImage;
        if (child.fontSize > 0)      out.fontSize  = child.fontSize;
        if (child.boldSet)   { out.bold = child.bold; out.boldSet = true; }
        if (child.italicSet) { out.italic = child.italic; out.italicSet = true; }
        if (child.underline)    out.underline   = true;
        if (child.displayNone)  out.displayNone = true;
        if (child.displayBlock) out.displayBlock = true;
        if (child.displayFlex)  out.displayFlex = true;
        if (child.marginTop    >= -1.5f) out.marginTop    = child.marginTop;
        if (child.marginRight  >= -1.5f) out.marginRight  = child.marginRight;
        if (child.marginBottom >= -1.5f) out.marginBottom = child.marginBottom;
        if (child.marginLeft   >= -1.5f) out.marginLeft   = child.marginLeft;
        if (child.paddingTop   >= 0) out.paddingTop    = child.paddingTop;
        if (child.paddingRight >= 0) out.paddingRight  = child.paddingRight;
        if (child.paddingBottom>= 0) out.paddingBottom = child.paddingBottom;
        if (child.paddingLeft  >= 0) out.paddingLeft   = child.paddingLeft;
        if (child.borderWidth  >= 0) out.borderWidth   = child.borderWidth;
        if (child.borderColor.valid) out.borderColor   = child.borderColor;
        if (child.borderRadius > 0)  out.borderRadius  = child.borderRadius;
        if (child.lineHeight   > 0)  out.lineHeight    = child.lineHeight;
        if (child.textAlignSet) { out.textAlign = child.textAlign; out.textAlignSet = true; }
        if (child.textTransformSet) { out.textTransform = child.textTransform; out.textTransformSet = true; }
        if (child.whiteSpaceSet) { out.whiteSpaceNowrap = child.whiteSpaceNowrap; out.whiteSpaceSet = true; }
        if (child.width        >= 0) out.width     = child.width;
        if (child.widthPercent >= 0) out.widthPercent = child.widthPercent;
        if (child.height       >= 0) out.height    = child.height;
        if (child.maxWidth     >= 0) out.maxWidth  = child.maxWidth;
        if (child.minWidth     >= 0) out.minWidth  = child.minWidth;
        if (child.minHeight    >= 0) out.minHeight = child.minHeight;
        if (child.floatMode    != 0) out.floatMode = child.floatMode;
        if (child.clearMode    != 0) out.clearMode = child.clearMode;
        if (child.positionMode != 0) out.positionMode = child.positionMode;
        if (child.overflowHidden) out.overflowHidden = true;
        if (child.topSet)    { out.top    = child.top;    out.topSet    = true; }
        if (child.rightSet)  { out.right  = child.right;  out.rightSet  = true; }
        if (child.bottomSet) { out.bottom = child.bottom; out.bottomSet = true; }
        if (child.leftSet)   { out.left   = child.left;   out.leftSet   = true; }
        return out;
    }
};
