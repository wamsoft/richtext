/**
 * Appearance.cpp
 * 
 * 描画外観の実装
 */

#include "richtext/Appearance.hpp"

#include <cstring>
#include <functional>

namespace richtext {

// ----------------------------------------------------------------------------
// ハッシュヘルパー
// ----------------------------------------------------------------------------

static size_t hashCombine(size_t seed, size_t value) {
    return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

static size_t hashFloat(float v) {
    if (v == 0.0f) v = 0.0f; // normalize -0
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return std::hash<uint32_t>{}(bits);
}

// ----------------------------------------------------------------------------
// DrawStyle 等価比較・ハッシュ
// ----------------------------------------------------------------------------

bool DrawStyle::operator==(const DrawStyle& o) const {
    if (type != o.type) return false;
    if (r != o.r || g != o.g || b != o.b || a != o.a) return false;
    if (offsetX != o.offsetX || offsetY != o.offsetY) return false;
    if (type == Type::Stroke) {
        if (strokeWidth != o.strokeWidth) return false;
        if (strokeCap != o.strokeCap) return false;
        if (strokeJoin != o.strokeJoin) return false;
        if (miterLimit != o.miterLimit) return false;
        if (dashPattern != o.dashPattern) return false;
        if (dashOffset != o.dashOffset) return false;
    }
    if (gradientType != o.gradientType) return false;
    if (gradientType != GradientType::None) {
        if (colorStops.size() != o.colorStops.size()) return false;
        for (size_t i = 0; i < colorStops.size(); ++i) {
            const auto& ca = colorStops[i];
            const auto& cb = o.colorStops[i];
            if (ca.offset != cb.offset || ca.r != cb.r || ca.g != cb.g ||
                ca.b != cb.b || ca.a != cb.a) return false;
        }
        if (gradX1 != o.gradX1 || gradY1 != o.gradY1 ||
            gradX2 != o.gradX2 || gradY2 != o.gradY2) return false;
        if (gradCx != o.gradCx || gradCy != o.gradCy || gradR != o.gradR) return false;
    }
    return true;
}

size_t DrawStyle::hash() const {
    size_t h = std::hash<int>{}(static_cast<int>(type));
    h = hashCombine(h, (static_cast<size_t>(r) << 24) | (g << 16) | (b << 8) | a);
    h = hashCombine(h, hashFloat(offsetX));
    h = hashCombine(h, hashFloat(offsetY));
    if (type == Type::Stroke) {
        h = hashCombine(h, hashFloat(strokeWidth));
        h = hashCombine(h, std::hash<int>{}(static_cast<int>(strokeCap)));
        h = hashCombine(h, std::hash<int>{}(static_cast<int>(strokeJoin)));
        h = hashCombine(h, hashFloat(miterLimit));
        for (float v : dashPattern) h = hashCombine(h, hashFloat(v));
        h = hashCombine(h, hashFloat(dashOffset));
    }
    h = hashCombine(h, std::hash<int>{}(static_cast<int>(gradientType)));
    if (gradientType != GradientType::None) {
        for (const auto& cs : colorStops) {
            h = hashCombine(h, hashFloat(cs.offset));
            h = hashCombine(h, (static_cast<size_t>(cs.r) << 24) |
                               (cs.g << 16) | (cs.b << 8) | cs.a);
        }
        h = hashCombine(h, hashFloat(gradX1));
        h = hashCombine(h, hashFloat(gradY1));
        h = hashCombine(h, hashFloat(gradX2));
        h = hashCombine(h, hashFloat(gradY2));
        h = hashCombine(h, hashFloat(gradCx));
        h = hashCombine(h, hashFloat(gradCy));
        h = hashCombine(h, hashFloat(gradR));
    }
    return h;
}

size_t Appearance::hash() const {
    size_t h = styles_.size();
    for (const auto& s : styles_) {
        h = hashCombine(h, s.hash());
    }
    return h;
}

// ----------------------------------------------------------------------------
// Appearance 実装
// ----------------------------------------------------------------------------

void Appearance::addFill(uint32_t argb, float offsetX, float offsetY) {
    styles_.push_back(DrawStyle::fill(argb, offsetX, offsetY));
}

void Appearance::addFill(const DrawStyle& style) {
    DrawStyle s = style;
    s.type = DrawStyle::Type::Fill;
    styles_.push_back(s);
}

void Appearance::addStroke(uint32_t argb, float width,
                           float offsetX, float offsetY) {
    styles_.push_back(DrawStyle::stroke(argb, width, offsetX, offsetY));
}

void Appearance::addStroke(const DrawStyle& style) {
    DrawStyle s = style;
    s.type = DrawStyle::Type::Stroke;
    styles_.push_back(s);
}

// ----------------------------------------------------------------------------
// タグ操作用メソッド
// ----------------------------------------------------------------------------

int Appearance::findBottomMainFillIndex() const {
    for (int i = 0; i < static_cast<int>(styles_.size()); i++) {
        if (styles_[i].type == DrawStyle::Type::Fill &&
            styles_[i].offsetX == 0.0f && styles_[i].offsetY == 0.0f) {
            return i;
        }
    }
    return -1;
}

void Appearance::setColor(uint32_t argb) {
    int mainIdx = findBottomMainFillIndex();
    if (mainIdx >= 0) {
        // mainIdx 以降の Fill をすべて削除（後ろから消す）
        for (int i = static_cast<int>(styles_.size()) - 1; i >= mainIdx; i--) {
            if (styles_[i].type == DrawStyle::Type::Fill) {
                styles_.erase(styles_.begin() + i);
            }
        }
    }
    addColor(argb);
}

void Appearance::addColor(uint32_t argb, float offsetX, float offsetY) {
    styles_.push_back(DrawStyle::fill(argb, offsetX, offsetY));
}

void Appearance::setOutline(uint32_t argb, float width,
                            float offsetX, float offsetY) {
    int mainIdx = findBottomMainFillIndex();
    int limit = (mainIdx >= 0) ? mainIdx : static_cast<int>(styles_.size());
    // limit より前の Stroke をすべて削除
    for (int i = limit - 1; i >= 0; i--) {
        if (styles_[i].type == DrawStyle::Type::Stroke) {
            styles_.erase(styles_.begin() + i);
        }
    }
    addOutline(argb, width, offsetX, offsetY);
}

void Appearance::addOutline(uint32_t argb, float width,
                            float offsetX, float offsetY) {
    DrawStyle style = DrawStyle::stroke(argb, width, offsetX, offsetY);
    style.strokeJoin = tvg::StrokeJoin::Round;

    int mainIdx = findBottomMainFillIndex();
    int limit = (mainIdx >= 0) ? mainIdx : static_cast<int>(styles_.size());

    // 既存 Stroke の一番下（最初の Stroke 位置）に挿入
    int insertPos = limit;
    for (int i = 0; i < limit; i++) {
        if (styles_[i].type == DrawStyle::Type::Stroke) {
            insertPos = i;
            break;
        }
    }
    styles_.insert(styles_.begin() + insertPos, style);
}

void Appearance::setShadow(uint32_t argb, float offsetX, float offsetY) {
    int mainIdx = findBottomMainFillIndex();
    int limit = (mainIdx >= 0) ? mainIdx : static_cast<int>(styles_.size());
    // limit より前の Fill をすべて削除
    for (int i = limit - 1; i >= 0; i--) {
        if (styles_[i].type == DrawStyle::Type::Fill) {
            styles_.erase(styles_.begin() + i);
        }
    }
    addShadow(argb, offsetX, offsetY);
}

void Appearance::addShadow(uint32_t argb, float offsetX, float offsetY) {
    styles_.insert(styles_.begin(), DrawStyle::fill(argb, offsetX, offsetY));
}

void Appearance::clear() {
    styles_.clear();
}

Appearance Appearance::defaultAppearance() {
    Appearance app;
    app.addFill(0xFFFFFFFF);  // 白
    return app;
}

Appearance Appearance::outlined(uint32_t fillColor, 
                                uint32_t strokeColor, 
                                float strokeWidth) {
    Appearance app;
    app.addStroke(strokeColor, strokeWidth);
    app.addFill(fillColor);
    return app;
}

} // namespace richtext
