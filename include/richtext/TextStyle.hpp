#ifndef RICHTEXT_TEXT_STYLE_HPP
#define RICHTEXT_TEXT_STYLE_HPP

#include <string>
#include <memory>
#include <cstdint>

#include <minikin/Layout.h>
#include <minikin/MinikinPaint.h>
#include <minikin/FontCollection.h>
#include <minikin/FontStyle.h>

namespace richtext {

/**
 * テキストスタイル
 * 
 * テキストの論理的なスタイルを定義する。
 * フォント、サイズ、ウェイト、字間などを指定。
 */
struct TextStyle {
    /**
     * フォントコレクション
     */
    std::shared_ptr<minikin::FontCollection> fontCollection;
    
    /**
     * フォントサイズ（ピクセル）
     */
    float fontSize = 16.0f;
    
    /**
     * フォントウェイト（100-900）
     * 400 = Normal, 700 = Bold
     */
    uint16_t fontWeight = 400;
    
    /**
     * イタリック
     */
    bool italic = false;
    
    /**
     * 字間（em単位、0.1 = 10%）
     */
    float letterSpacing = 0.0f;
    
    /**
     * 語間（em単位）
     */
    float wordSpacing = 0.0f;
    
    /**
     * ロケールID（行分割ルールに影響）
     */
    uint32_t localeId = 0;
    
    /**
     * 水平スケール（通常 1.0）
     */
    float scaleX = 1.0f;
    
    /**
     * 斜体のスキュー値（0.0 = 通常、負の値で右傾斜）
     */
    float skewX = 0.0f;

    /**
     * 双方向テキスト制御
     * DEFAULT_LTR: テキスト内容から自動判定（LTR優先）
     */
    minikin::Bidi bidi = minikin::Bidi::DEFAULT_LTR;
    
    // ------------------------------------------------------------------
    // メソッド
    // ------------------------------------------------------------------
    
    /**
     * デフォルトコンストラクタ
     */
    TextStyle() = default;
    
    /**
     * minikin::MinikinPaint への変換
     * @param paint 出力先
     */
    void applyTo(minikin::MinikinPaint& paint) const;
    
    /**
     * minikin::FontStyle の取得
     */
    minikin::FontStyle getFontStyle() const;
    
    /**
     * コピー
     */
    TextStyle clone() const { return *this; }
};

} // namespace richtext

#endif // RICHTEXT_TEXT_STYLE_HPP
