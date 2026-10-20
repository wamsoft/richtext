#ifndef RICHTEXT_STYLED_LAYOUT_HPP
#define RICHTEXT_STYLED_LAYOUT_HPP

#include <string>
#include <vector>
#include <map>

#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/TagParser.hpp"
#include "richtext/TimingInfo.hpp"
#include "richtext/LinkRegion.hpp"
#include "richtext/vertical/VerticalParagraphLayout.hpp"

namespace richtext {

/**
 * スタイルタグ付きテキストのレイアウト結果
 *
 * drawStyledText() のレイアウト処理を分離したクラス。
 * タグ解析・行分割・セグメント分割の結果を保持し、
 * TextRenderer::drawStyledLayout() で描画する。
 */
class StyledLayout {
public:
    /**
     * セグメントレイアウト情報
     */
    struct SegmentLayout {
        size_t spanIdx;         ///< spans 内のインデックス
        size_t segStart;        ///< plainText 内の開始位置
        size_t segEnd;          ///< plainText 内の終了位置
        float yOffset;          ///< Y方向オフセット
        float measuredWidth;    ///< MeasuredText ベースの幅
        TextLayout layout;      ///< セグメントの TextLayout
    };

    /**
     * 行レイアウト情報
     */
    struct LineLayout {
        size_t lineIdx;                         ///< 行インデックス
        float totalWidth;                       ///< 行の総幅
        std::vector<SegmentLayout> segments;    ///< セグメント配列
    };

    /**
     * 組み方向
     */
    enum class WritingMode {
        Horizontal = 0,     ///< 横組み
        VerticalRl = 1,     ///< 縦組み・列は右から左へ
        VerticalLr = 2,     ///< 縦組み・列は左から右へ
    };

    /**
     * 文字ごとの最終的な配置（layout() で作る）
     *
     * 横組み・縦組みとも、本文・ルビ・注記のすべてのグリフをこの形で持つ。
     * glyph.x / glyph.y は layout() の描画原点 (drawStyledLayout の x, y) からの位置で、
     * 回転・倍率・注記の文字サイズ (glyph.fontSize) もそのまま入っている。
     * 描画は GlyphRenderer::renderGlyph(glyph, 原点X, 原点Y, getPlacementStyle(p), getPlacementAppearance(p))。
     */
    struct GlyphPlacement {
        enum class Kind {
            Base,           ///< 本文
            Ruby,           ///< ルビ
            Annotation,     ///< 縦組みの注記（ルビ・縦中横の文字以外の注記グリフ）
        };
        GlyphInfo glyph;
        size_t styleIndex = 0;          ///< getPlacementStyles() の添字
        size_t spanIdx = SIZE_MAX;      ///< 外観を引く span（SIZE_MAX なら既定の外観）
        size_t charIndex = 0;           ///< プレーンテキスト上の位置（ルビ・注記は親文字の先頭）
        Kind kind = Kind::Base;

        /**
         * 下線・取り消し線（<u> <s>）の、この文字が受け持つ部分
         *
         * 線は文字ごとに切って持つので、文字と一緒に 1 文字ずつ出る。
         * 座標はペン位置 (glyph.x, glyph.y) からの矩形。隣の文字の分とすき間が
         * 空かないよう、続きがあるときは 1px 長くしてある。
         * 文字と同じ外観（影・縁取り・塗り）で、DrawStyle ごとに文字と交互に描く
         * （decorationRect()）。縁取りは線の両側と、続きの無い端にだけ付ける。
         */
        struct Decoration {
            float x = 0, y = 0, width = 0, height = 0;
            bool vertical = false;      ///< 縦組みの線（送り方向が y）
            bool capStart = true;       ///< 線の始まり（縁取りを付ける）
            bool capEnd = true;         ///< 線の終わり（縁取りを付ける）
        };
        uint8_t decorationCount = 0;    ///< 0〜2（下線と取り消し線）
        Decoration decorations[2];
        uint32_t decorationColor = 0;   ///< 外観が空のときの色（ARGB）
    };

    /**
     * 下線・取り消し線を DrawStyle 1 つ分で描く矩形（ペン位置からの相対）
     * @return 描くものがあれば true
     */
    static bool decorationRect(const GlyphPlacement::Decoration& d, const DrawStyle& ds,
                               float& x, float& y, float& w, float& h) {
        x = d.x + ds.offsetX;
        y = d.y + ds.offsetY;
        w = d.width;
        h = d.height;
        if (ds.type == DrawStyle::Type::Stroke) {
            const float e = ds.strokeWidth / 2.0f;
            if (e <= 0.0f) return false;
            if (!d.vertical) {
                y -= e; h += e * 2.0f;
                if (d.capStart) { x -= e; w += e; }
                if (d.capEnd) w += e;
            } else {
                x -= e; w += e * 2.0f;
                if (d.capStart) { y -= e; h += e; }
                if (d.capEnd) h += e;
            }
        }
        return w > 0.0f && h > 0.0f;
    }

    StyledLayout() = default;

    /**
     * 組み方向の設定（layout() 前に呼ぶ。既定は横組み）
     *
     * 縦組みでは maxHeight が列の長さ、maxWidth が列を並べる幅になる。
     * 列の中の揃えは hAlign (Left=行頭 / Center / Right=行末) を使い、vAlign は使わない。
     * 縦組みの文字の大きさ・太さ・フォントは default スタイルで揃い、
     * タグで変えられるのは外観（色・縁取り・影）とルビ・縦中横 (<tcy>)。
     */
    void setWritingMode(WritingMode mode) { writingMode_ = mode; }
    WritingMode getWritingMode() const { return writingMode_; }
    bool isVertical() const { return writingMode_ != WritingMode::Horizontal; }

    /**
     * TagParser オプションの設定（layout() 前に呼ぶ）
     */
    void setParserOptions(const TagParser::ParseOptions& opts) { parserOptions_ = opts; }

    /**
     * eval タグ用コールバックの設定（layout() 前に呼ぶ）
     */
    void setEvalCallback(EvalCallback cb) { evalCallback_ = std::move(cb); }

    /**
     * ラベル解決コールバックの設定（layout() 前に呼ぶ）
     */
    void setLabelResolver(LabelResolver cb) { labelResolver_ = std::move(cb); }

    /**
     * 行分割戦略の設定（layout() 前に呼ぶ）
     * 既定は ParagraphLayout と同じ HighQuality。
     */
    void setBreakStrategy(ParagraphLayout::BreakStrategy strategy) { para_.setBreakStrategy(strategy); }

    /** 行分割戦略の取得 */
    ParagraphLayout::BreakStrategy getBreakStrategy() const { return para_.getBreakStrategy(); }

    /**
     * レイアウト実行
     * @param text スタイルタグ付きテキスト（UTF-16）
     * @param maxWidth 最大幅
     * @param maxHeight 最大高さ
     * @param hAlign 水平アライン
     * @param vAlign 垂直アライン
     * @param styles スタイル名 → TextStyle のマップ
     * @param appearances スタイル名 → Appearance のマップ
     * @param lineSpacing 行間
     */
    void layout(const std::u16string& text,
                float maxWidth, float maxHeight,
                ParagraphLayout::HAlign hAlign,
                ParagraphLayout::VAlign vAlign,
                const std::map<std::string, TextStyle>& styles,
                const std::map<std::string, Appearance>& appearances,
                float lineSpacing = 0.0f);

    // ------------------------------------------------------------------
    // 結果取得
    // ------------------------------------------------------------------

    /** パース結果の取得 */
    const TagParser::ParseResult& getParsed() const { return parsed_; }

    /** パラグラフレイアウトの取得 */
    const ParagraphLayout& getParagraphLayout() const { return para_; }

    /** 行レイアウト配列の取得 */
    const std::vector<LineLayout>& getLineLayouts() const { return lineLayouts_; }

    /** 行数（縦組みでは列数） */
    size_t getLineCount() const { return isVertical() ? vpara_.getLineCount() : lineLayouts_.size(); }

    /** 文字ごとの配置（本文・ルビ・注記） */
    const std::vector<GlyphPlacement>& getPlacements() const { return placements_; }

    /** 配置が参照するスタイル */
    const std::vector<TextStyle>& getPlacementStyles() const { return placementStyles_; }
    const TextStyle& getPlacementStyle(const GlyphPlacement& p) const { return placementStyles_[p.styleIndex]; }

    /** 配置の外観 */
    const Appearance& getPlacementAppearance(const GlyphPlacement& p) const {
        return p.spanIdx < parsed_.spans.size() ? parsed_.spans[p.spanIdx].appearance : defaultAppearance_;
    }

    /** 描画範囲（描画原点からの位置。縁取り・影は含まない） */
    struct Bounds { float x = 0, y = 0, width = 0, height = 0; };
    const Bounds& getBounds() const { return bounds_; }

    /** 縦組みのレイアウト結果 */
    const vertical::VerticalParagraphLayout& getVerticalLayout() const { return vpara_; }

    /** 水平アライン */
    ParagraphLayout::HAlign getHAlign() const { return hAlign_; }

    /** 垂直アライン */
    ParagraphLayout::VAlign getVAlign() const { return vAlign_; }

    /** 最大幅 */
    float getMaxWidth() const { return maxWidth_; }

    /** 最大高さ */
    float getMaxHeight() const { return maxHeight_; }

    /** 総文字数（ユニーク charIndex 数） */
    size_t getTotalCharCount() const { return totalCharCount_; }

    /** TagParser オプションの取得 */
    const TagParser::ParseOptions& getParserOptions() const { return parserOptions_; }

    /** レイアウト済みかどうか */
    bool isValid() const { return valid_; }

    // ------------------------------------------------------------------
    // メタデータアクセサ（TagParser が生成した情報）
    // ------------------------------------------------------------------

    /** タイミング情報 */
    const std::vector<TimingEntry>& getTimings() const { return parsed_.timings; }

    /** リンク情報 */
    const std::vector<LinkInfo>& getLinks() const { return parsed_.links; }

    /** グラフィック情報 */
    const std::vector<GraphInfo>& getGraphics() const { return parsed_.graphics; }

    /**
     * リンク矩形領域を構築
     * レイアウト完了後に呼び出し、文字位置情報から LinkRegion を生成する。
     */
    std::vector<LinkRegion> buildLinkRegions() const;

    // ------------------------------------------------------------------
    // タイミング解決
    // ------------------------------------------------------------------

    /**
     * タイミング情報を解決し、各文字の表示開始時間を計算する
     * @param timeScale 時間スケール係数
     * @param widthTimeScale 文字幅による時間補正を行うか
     */
    void resolveAllTimings(float timeScale = 1.0f,
                           bool widthTimeScale = false);

    /** 解決済みタイミング配列（未解決時は自動解決） */
    const std::vector<ResolvedTiming>& getResolvedTimings() const;

    /** キー入力待ち情報（未解決時は自動解決） */
    const std::vector<KeyWaitInfo>& getKeyWaits() const;

    /** 全体の最大表示遅延時間（ms）（未解決時は自動解決） */
    float getTotalRenderDelay() const;

    /**
     * 指定時間までに表示すべき文字数を計算（未解決時は自動解決）
     * @param time 経過時間（ms）
     * @return 表示文字数
     */
    int calcShowCount(float time) const;

private:
    TagParser::ParseResult parsed_;
    ParagraphLayout para_;
    std::vector<LineLayout> lineLayouts_;
    TagParser::ParseOptions parserOptions_;
    EvalCallback evalCallback_;
    LabelResolver labelResolver_;

    ParagraphLayout::HAlign hAlign_ = ParagraphLayout::HAlign::Left;
    ParagraphLayout::VAlign vAlign_ = ParagraphLayout::VAlign::Top;
    float maxWidth_ = 0;
    float maxHeight_ = 0;

    size_t totalCharCount_ = 0;
    bool valid_ = false;

    WritingMode writingMode_ = WritingMode::Horizontal;
    vertical::VerticalParagraphLayout vpara_;
    Appearance defaultAppearance_;
    std::vector<GlyphPlacement> placements_;
    std::vector<TextStyle> placementStyles_;
    Bounds bounds_;

    void buildHorizontalPlacements();
    void layoutVertical(const TextStyle& defaultStyle, float lineSpacing);
    size_t findSpan(size_t charIndex) const;
    uint32_t decorationColorOf(size_t spanIdx) const;

    // タイミング解決結果
    mutable std::vector<ResolvedTiming> resolvedTimings_;
    mutable std::vector<KeyWaitInfo> resolvedKeyWaits_;
    mutable float totalRenderDelay_ = 0;
    mutable bool timingsResolved_ = false;

    void ensureTimingsResolved() const;
};

} // namespace richtext

#endif // RICHTEXT_STYLED_LAYOUT_HPP
