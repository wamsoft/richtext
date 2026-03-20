/**
 * TextStyle.cpp
 * 
 * テキストスタイルの実装
 */

#include "richtext/TextStyle.hpp"

namespace richtext {

void TextStyle::applyTo(minikin::MinikinPaint& paint) const {
    if (fontCollection) {
        paint.font = fontCollection;
    }
    
    paint.size = fontSize;
    paint.fontStyle = getFontStyle();
    paint.localeListId = localeId;
    
    // 字間の設定（scaleXが0だとletterSpacingが効かない）
    paint.scaleX = scaleX;
    paint.letterSpacing = letterSpacing;
    paint.wordSpacing = wordSpacing;
    paint.skewX = skewX;
}

minikin::FontStyle TextStyle::getFontStyle() const {
    minikin::FontStyle::Slant slant = italic 
        ? minikin::FontStyle::Slant::ITALIC 
        : minikin::FontStyle::Slant::UPRIGHT;
    
    return minikin::FontStyle(fontWeight, slant);
}

} // namespace richtext
