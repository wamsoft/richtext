/**
 * Appearance.cpp
 * 
 * 描画外観の実装
 */

#include "richtext/Appearance.hpp"

namespace richtext {

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
