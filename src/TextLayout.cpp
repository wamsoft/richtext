/**
 * TextLayout.cpp
 * 
 * 1行テキストのレイアウト処理
 */

#include "richtext/TextLayout.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/FontManager.hpp"

#include <minikin/MinikinExtent.h>
#include <minikin/MinikinRect.h>

namespace richtext {

//------------------------------------------------------------------------------
// レイアウト実行
//------------------------------------------------------------------------------

void TextLayout::layout(const std::u16string& text, const TextStyle& style) {
    text_ = text;
    style_ = style;
    doLayout();
}

void TextLayout::layout(std::u16string&& text, const TextStyle& style) {
    text_ = std::move(text);
    style_ = style;
    doLayout();
}

//------------------------------------------------------------------------------
// 内部実装
//------------------------------------------------------------------------------

void TextLayout::doLayout() {
    if (text_.empty() || !style_.fontCollection) {
        return;
    }
    
    // MinikinPaint の設定
    minikin::MinikinPaint paint(style_.fontCollection);
    style_.applyTo(paint);
    
    // レイアウト実行
    minikin::StartHyphenEdit startHyphen = minikin::StartHyphenEdit::NO_EDIT;
    minikin::EndHyphenEdit endHyphen = minikin::EndHyphenEdit::NO_EDIT;
    
    // U16StringPiece は uint16_t* を要求するので reinterpret_cast
    const uint16_t* textData = reinterpret_cast<const uint16_t*>(text_.data());
    minikin::U16StringPiece textPiece(textData, static_cast<uint32_t>(text_.size()));
    minikin::Range range(0, static_cast<uint32_t>(text_.size()));
    
    // Layout オブジェクトを構築（コンストラクタがdoLayoutを呼ぶ）
    layout_ = minikin::Layout(
        textPiece,
        range,
        minikin::Bidi::LTR,  // TODO: 双方向テキスト対応
        paint,
        startHyphen,
        endHyphen
    );
    
    // 結果をキャッシュ
    cacheMetrics(paint);
    buildGlyphInfos();
}

void TextLayout::cacheMetrics(const minikin::MinikinPaint& paint) {
    // 幅
    width_ = layout_.getAdvance();
    
    // Extent（ascent/descent）はフォントから取得
    // 最初のグリフのフォントを使用
    if (layout_.nGlyphs() > 0) {
        const minikin::MinikinFont* font = layout_.getFont(0);
        if (font) {
            minikin::MinikinExtent extent;
            minikin::FontFakery fakery = layout_.getFakery(0);
            font->GetFontExtent(&extent, paint, fakery);
            ascent_ = extent.ascent;   // 負の値
            descent_ = extent.descent; // 正の値
        }
    }
    
    // バウンディングボックス
    minikin::MinikinRect rect;
    layout_.getBounds(&rect);
    bounds_.left = rect.mLeft;
    bounds_.top = rect.mTop;
    bounds_.right = rect.mRight;
    bounds_.bottom = rect.mBottom;
}

void TextLayout::buildGlyphInfos() {
    glyphs_.clear();
    
    size_t glyphCount = layout_.nGlyphs();
    glyphs_.reserve(glyphCount);
    
    for (size_t i = 0; i < glyphCount; ++i) {
        GlyphInfo info;
        
        // グリフID
        info.glyphId = layout_.getGlyphId(static_cast<int>(i));
        
        // フォント（MinikinFont* → FontFace*）
        const minikin::MinikinFont* minikinFont = layout_.getFont(static_cast<int>(i));
        info.font = static_cast<const FontFace*>(minikinFont);
        
        // 座標
        info.x = layout_.getX(static_cast<int>(i));
        info.y = layout_.getY(static_cast<int>(i));
        
        // 擬似スタイル
        info.fakery = layout_.getFakery(static_cast<int>(i));
        
        // 文字インデックス（グリフ番号をそのまま使用）
        // Note: minikin::Layout には getCharIndex がないため
        info.charIndex = i;
        
        // アドバンス（次のグリフとの差分から計算）
        if (i + 1 < glyphCount) {
            info.advance = layout_.getX(static_cast<int>(i) + 1) - layout_.getX(static_cast<int>(i));
        } else {
            info.advance = layout_.getAdvance() - layout_.getX(static_cast<int>(i));
        }
        
        glyphs_.push_back(info);
    }
}

} // namespace richtext
