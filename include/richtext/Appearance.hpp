#ifndef RICHTEXT_APPEARANCE_HPP
#define RICHTEXT_APPEARANCE_HPP

#include <vector>
#include <cstdint>

#include <thorvg.h>

namespace richtext {

/**
 * 描画スタイル
 * 
 * 単一の塗りまたはストロークを定義する。
 */
struct DrawStyle {
    /**
     * 描画タイプ
     */
    enum class Type {
        Fill,       // 塗り
        Stroke      // ストローク（縁取り）
    };
    
    /**
     * グラデーションタイプ
     */
    enum class GradientType {
        None,       // 単色
        Linear,     // 線形グラデーション
        Radial      // 放射状グラデーション
    };
    
    // ------------------------------------------------------------------
    // 基本設定
    // ------------------------------------------------------------------
    
    /**
     * 描画タイプ
     */
    Type type = Type::Fill;
    
    /**
     * 色 (RGBA)
     */
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    
    /**
     * オフセット（影効果等に使用）
     */
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    
    // ------------------------------------------------------------------
    // ストローク設定（type == Stroke の場合）
    // ------------------------------------------------------------------
    
    /**
     * ストローク幅
     */
    float strokeWidth = 1.0f;
    
    /**
     * 線端キャップ
     */
    tvg::StrokeCap strokeCap = tvg::StrokeCap::Square;
    
    /**
     * 線結合
     */
    tvg::StrokeJoin strokeJoin = tvg::StrokeJoin::Miter;
    
    /**
     * マイター限界
     */
    float miterLimit = 4.0f;
    
    /**
     * 破線パターン
     */
    std::vector<float> dashPattern;
    
    /**
     * 破線オフセット
     */
    float dashOffset = 0.0f;
    
    // ------------------------------------------------------------------
    // グラデーション設定
    // ------------------------------------------------------------------
    
    /**
     * グラデーションタイプ
     */
    GradientType gradientType = GradientType::None;
    
    /**
     * カラーストップ
     */
    std::vector<tvg::Fill::ColorStop> colorStops;
    
    /**
     * 線形グラデーション: 開始点・終了点
     */
    float gradX1 = 0.0f;
    float gradY1 = 0.0f;
    float gradX2 = 0.0f;
    float gradY2 = 0.0f;
    
    /**
     * 放射状グラデーション: 中心・半径
     */
    float gradCx = 0.0f;
    float gradCy = 0.0f;
    float gradR = 0.0f;
    
    // ------------------------------------------------------------------
    // ヘルパーメソッド
    // ------------------------------------------------------------------
    
    /**
     * ARGB 値から色を設定
     */
    void setColor(uint32_t argb) {
        a = (argb >> 24) & 0xFF;
        r = (argb >> 16) & 0xFF;
        g = (argb >> 8) & 0xFF;
        b = argb & 0xFF;
    }
    
    /**
     * ARGB 値として取得
     */
    uint32_t getColor() const {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               static_cast<uint32_t>(b);
    }
    
    /**
     * 単色塗りスタイル作成
     */
    static DrawStyle fill(uint32_t argb, float ox = 0, float oy = 0) {
        DrawStyle style;
        style.type = Type::Fill;
        style.setColor(argb);
        style.offsetX = ox;
        style.offsetY = oy;
        return style;
    }
    
    /**
     * ストロークスタイル作成
     */
    static DrawStyle stroke(uint32_t argb, float width, float ox = 0, float oy = 0) {
        DrawStyle style;
        style.type = Type::Stroke;
        style.setColor(argb);
        style.strokeWidth = width;
        style.offsetX = ox;
        style.offsetY = oy;
        return style;
    }
};

/**
 * 描画外観
 * 
 * 複数の DrawStyle を組み合わせて装飾効果を実現する。
 * 登録順に描画される（後に追加したものが上に描画される）。
 */
class Appearance {
public:
    /**
     * デフォルトコンストラクタ
     */
    Appearance() = default;
    
    /**
     * 塗りを追加
     * @param argb ARGB色値
     * @param offsetX X方向オフセット
     * @param offsetY Y方向オフセット
     */
    void addFill(uint32_t argb, float offsetX = 0, float offsetY = 0);
    
    /**
     * 塗りを追加（詳細設定）
     * @param style 描画スタイル
     */
    void addFill(const DrawStyle& style);
    
    /**
     * ストロークを追加
     * @param argb ARGB色値
     * @param width 線幅
     * @param offsetX X方向オフセット
     * @param offsetY Y方向オフセット
     */
    void addStroke(uint32_t argb, float width,
                   float offsetX = 0, float offsetY = 0);
    
    /**
     * ストロークを追加（詳細設定）
     * @param style 描画スタイル
     */
    void addStroke(const DrawStyle& style);
    
    // ------------------------------------------------------------------
    // タグ操作用メソッド
    // ------------------------------------------------------------------

    /**
     * テキスト色の設定（既存の通常Fillを置換）
     * 一番下のオフセット0 Fillから上のFillをすべて削除した後 addColor()
     */
    void setColor(uint32_t argb);

    /**
     * テキスト色の追加（一番上＝最前面に追加）
     */
    void addColor(uint32_t argb, float offsetX = 0, float offsetY = 0);

    /**
     * 縁取りの設定（既存のStrokeを置換）
     * 一番下のオフセット0 Fillの下にあるStrokeをすべて削除した後 addOutline()
     */
    void setOutline(uint32_t argb, float width,
                    float offsetX = 0, float offsetY = 0);

    /**
     * 縁取りの追加（既存Strokeの一番下＝最背面に追加）
     */
    void addOutline(uint32_t argb, float width,
                    float offsetX = 0, float offsetY = 0);

    /**
     * 影の設定（既存の影Fillを置換）
     * 一番下のオフセット0 Fillの下にあるFillをすべて削除した後 addShadow()
     */
    void setShadow(uint32_t argb, float offsetX, float offsetY);

    /**
     * 影の追加（一番後ろ＝最背面に追加）
     */
    void addShadow(uint32_t argb, float offsetX, float offsetY);

    /**
     * 全スタイルをクリア
     */
    void clear();
    
    /**
     * スタイルが空かどうか
     */
    bool isEmpty() const { return styles_.empty(); }
    
    /**
     * スタイル数
     */
    size_t size() const { return styles_.size(); }
    
    /**
     * スタイル配列の取得
     */
    const std::vector<DrawStyle>& getStyles() const { return styles_; }
    
    /**
     * デフォルトの外観を作成（白塗り）
     */
    static Appearance defaultAppearance();
    
    /**
     * 縁取り外観を作成
     * @param fillColor 塗り色
     * @param strokeColor 縁取り色
     * @param strokeWidth 縁取り幅
     */
    static Appearance outlined(uint32_t fillColor, 
                               uint32_t strokeColor, 
                               float strokeWidth);

private:
    std::vector<DrawStyle> styles_;

    /**
     * 一番下（最初）のオフセット0 Fill のインデックスを返す（未発見時 -1）
     */
    int findBottomMainFillIndex() const;
};

} // namespace richtext

#endif // RICHTEXT_APPEARANCE_HPP
