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
