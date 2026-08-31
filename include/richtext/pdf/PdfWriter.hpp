#ifndef RICHTEXT_PDF_WRITER_HPP
#define RICHTEXT_PDF_WRITER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/vertical/BlockLayout.hpp"
#include "richtext/vertical/VerticalParagraphLayout.hpp"

/**
 * PdfWriter — 組版結果を PDF として書き出す
 *
 * 組版層の出力（`GlyphInfo` の列）をそのまま受け取る。ラスタライズ backend
 * （GlyphRenderer）と並列に置かれた出力先で、**同じ組版結果から画面と PDF が
 * 出る**ようにするのがこの層の目的（縦組み設計.md 1.2 / 7）。
 *
 * 既存 PDF ライブラリに「文字列」を渡す方式は採らない。ライブラリ側が
 * 再シェイピングして組版結果が崩れるため、グリフ ID を直接書く。
 *
 *  - `Identity-H` + CIDFontType0（CFF）/ CIDFontType2（glyf）でフォント埋め込み
 *  - 1 グリフずつ `Tm` で配置する。縦組みでも `Identity-V` は使わない
 *    （縦メトリクスは組版層で計算済みなので、PDF 側に再計算させない）
 *  - 横倒しが必要なグリフは `Tm` に回転行列が入る
 *  - サブセット化はしない（full embed）
 *
 * 座標は呼び出し側と同じ**左上原点・y-down のピクセル**で渡す。PDF の
 * y-up への変換はこのクラスが行う（1px = 1pt として扱う）。
 */
namespace richtext::pdf {

class PdfWriter {
public:
    PdfWriter();
    ~PdfWriter();

    PdfWriter(const PdfWriter&) = delete;
    PdfWriter& operator=(const PdfWriter&) = delete;

    // ------------------------------------------------------------------
    // 文書情報
    // ------------------------------------------------------------------

    void setTitle(std::string title);
    void setAuthor(std::string author);
    void setCreator(std::string creator);

    /**
     * ToUnicode CMap を埋め込むか（既定 true）
     *
     * 埋め込むと PDF 上でテキスト検索・コピーができる。glyph → 文字の対応は
     * 描画時に渡された原文から取るので、原文を渡さない描画では埋まらない。
     */
    void setEmbedToUnicode(bool embed);

    // ------------------------------------------------------------------
    // ページ
    // ------------------------------------------------------------------

    /**
     * ページを開始する
     * @param width,height ページの大きさ（ピクセル＝ポイント）
     */
    void beginPage(float width, float height);

    /**
     * ページを閉じる。beginPage と対で呼ぶ
     */
    void endPage();

    size_t getPageCount() const;

    // ------------------------------------------------------------------
    // 描画
    // ------------------------------------------------------------------

    /**
     * グリフ列を描く
     *
     * `GlyphRenderer::renderGlyphs()` と同じ引数・同じ座標系。
     *
     * @param sourceText 原文（省略可）。ToUnicode CMap のために使う
     */
    void drawGlyphs(const std::vector<GlyphInfo>& glyphs,
                    float x, float y,
                    const TextStyle& style,
                    const Appearance& appearance,
                    const std::u16string* sourceText = nullptr);

    /**
     * 縦組み段落を描く
     * @param originX 1 列目の縦ベースラインのX、@param originY 行頭のY
     */
    void drawVerticalParagraph(const vertical::VerticalParagraphLayout& para,
                               float originX, float originY,
                               const Appearance& appearance);

    /**
     * 流し込み済みブロックのコンテナ 1 つ分（＝1 ページ分）を描く
     */
    void drawBlockContainer(const vertical::BlockLayout& block,
                            size_t containerIndex,
                            float offsetX, float offsetY,
                            const Appearance& appearance);

    /**
     * 矩形（塗り／枠線）。色は ARGB
     */
    void drawRect(float x, float y, float width, float height,
                  uint32_t fillColor, uint32_t strokeColor = 0,
                  float strokeWidth = 0.0f);

    // ------------------------------------------------------------------
    // 出力
    // ------------------------------------------------------------------

    /**
     * PDF のバイト列を組み立てる
     */
    std::string build();

    /**
     * ファイルへ書き出す
     */
    bool save(const std::string& path);

    /**
     * 直近の失敗理由（埋め込めなかったフォント等）
     */
    const std::vector<std::string>& getWarnings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace richtext::pdf

#endif // RICHTEXT_PDF_WRITER_HPP
