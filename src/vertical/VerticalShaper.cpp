/**
 * VerticalShaper.cpp
 *
 * 縦組みのシェイピング
 *
 * minikin::Layout は LTR / RTL しか扱えない（LayoutCore.cpp が
 * HB_DIRECTION_LTR / RTL しか設定しない）ので、縦組みでは通さない。
 * フォントフォールバックの解決だけ FontCollection::itemize() を再利用し、
 * シェイピングは HarfBuzz を直接叩く。
 *
 * MinikinPaint::fontFeatureSettings に vert を渡す方式は採らない。非空だと
 * レイアウトキャッシュが無効化されるうえ、vkrn / vpal は横方向の GPOS として
 * は効かないため。
 */

#include "richtext/vertical/VerticalShaper.hpp"

#include "richtext/FontFace.hpp"

#include <minikin/FamilyVariant.h>
#include <minikin/Font.h>
#include <minikin/FontCollection.h>
#include <minikin/HbUtils.h>
#include <minikin/U16StringPiece.h>

#include <hb.h>

#include <algorithm>
#include <unordered_map>

namespace richtext::vertical {

namespace {

using minikin::HBFixedToFloat;
using minikin::HbBufferUniquePtr;
using minikin::HbFontUniquePtr;

/// UTF-16 の位置 i からコードポイントを取り出す（サロゲートペア対応）
inline char32_t codePointAt(const std::u16string& text, size_t i, size_t& length) {
    const char16_t c = text[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size()) {
        const char16_t c2 = text[i + 1];
        if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
            length = 2;
            return 0x10000 + (static_cast<char32_t>(c - 0xD800) << 10)
                           + static_cast<char32_t>(c2 - 0xDC00);
        }
    }
    length = 1;
    return c;
}

/// 文字ごとの向きを決める（UTF-16 単位。サロゲートペアは両方に同じ値を入れる）
std::vector<CharOrientation> resolveOrientations(const std::u16string& text,
                                                 TextOrientation mode) {
    std::vector<CharOrientation> out(text.size(), CharOrientation::Upright);
    if (mode == TextOrientation::Upright) {
        return out;
    }
    if (mode == TextOrientation::Sideways) {
        std::fill(out.begin(), out.end(), CharOrientation::Rotated);
        return out;
    }
    for (size_t i = 0; i < text.size();) {
        size_t len = 1;
        const char32_t cp = codePointAt(text, i, len);
        const CharOrientation o = getCharOrientation(cp);
        for (size_t k = 0; k < len && i + k < out.size(); ++k) {
            out[i + k] = o;
        }
        i += len;
    }
    return out;
}

/// hb_font のキャッシュ（1 回の shape 呼び出し内で使い回す）
class HbFontCache {
public:
    hb_font_t* get(const minikin::Font* font, float size, float scaleX) {
        auto it = fonts_.find(font);
        if (it != fonts_.end()) {
            return it->second.get();
        }
        // minikin と違って font funcs は差し替えない。既定の ot funcs のまま
        // にしておくことで vmtx / VORG による縦メトリクスが効く。
        HbFontUniquePtr sub(hb_font_create_sub_font(font->baseFont().get()));
        hb_font_set_ppem(sub.get(), static_cast<unsigned>(size * scaleX),
                         static_cast<unsigned>(size));
        hb_font_set_scale(sub.get(), minikin::HBFloatToFixed(size * scaleX),
                          minikin::HBFloatToFixed(size));
        hb_font_t* rawFont = sub.get();
        fonts_.emplace(font, std::move(sub));
        return rawFont;
    }

private:
    std::unordered_map<const minikin::Font*, HbFontUniquePtr> fonts_;
};

} // namespace

VerticalShaper::Result VerticalShaper::shape(const std::u16string& text,
                                             const TextStyle& style,
                                             TextOrientation orientation) {
    Result result;
    if (text.empty() || !style.fontCollection) {
        return result;
    }
    result.sourceText = text;

    const float size = style.fontSize;
    const float scaleX = (style.scaleX > 0.0f) ? style.scaleX : 1.0f;
    const float letterSpacing = style.letterSpacing * size;

    const std::vector<CharOrientation> orient = resolveOrientations(text, orientation);

    // フォントフォールバックの解決は minikin に任せる
    const uint16_t* raw = reinterpret_cast<const uint16_t*>(text.data());
    const minikin::U16StringPiece piece(raw, static_cast<uint32_t>(text.size()));
    const std::vector<minikin::FontCollection::Run> runs =
            style.fontCollection->itemize(piece, style.getFontStyle(), style.localeId,
                                          minikin::FamilyVariant::DEFAULT);

    HbFontCache fontCache;
    HbBufferUniquePtr buffer(hb_buffer_create());

    float pen = 0.0f;               // v 座標（行頭からの送り）
    float minU = 0.0f, maxU = 0.0f; // u 方向の張り出し
    bool haveExtent = false;

    for (const minikin::FontCollection::Run& run : runs) {
        if (run.start >= run.end) continue;

        const minikin::Font* font = run.fakedFont.font;
        const FontFace* face = static_cast<const FontFace*>(font->typeface().get());
        hb_font_t* hbFont = fontCache.get(font, size, scaleX);

        // フォントランをさらに向きで分割する
        for (int segStart = run.start; segStart < run.end;) {
            const CharOrientation segOrient = orient[segStart];
            int segEnd = segStart + 1;
            while (segEnd < run.end && orient[segEnd] == segOrient) {
                ++segEnd;
            }

            const bool upright = (segOrient == CharOrientation::Upright);

            // 前後を文脈として渡しつつ、シェイプ対象は [segStart, segEnd)
            hb_buffer_clear_contents(buffer.get());
            hb_buffer_add_utf16(buffer.get(), raw, static_cast<int>(text.size()),
                                static_cast<unsigned>(segStart),
                                static_cast<int>(segEnd - segStart));
            hb_buffer_guess_segment_properties(buffer.get());
            // guess_segment_properties がスクリプトから決めた方向を上書きする
            hb_buffer_set_direction(buffer.get(),
                                    upright ? HB_DIRECTION_TTB : HB_DIRECTION_LTR);

            hb_shape(hbFont, buffer.get(), nullptr, 0);

            unsigned int numGlyphs = 0;
            const hb_glyph_info_t* info =
                    hb_buffer_get_glyph_infos(buffer.get(), &numGlyphs);
            const hb_glyph_position_t* pos =
                    hb_buffer_get_glyph_positions(buffer.get(), nullptr);

            if (numGlyphs == 0) {
                segStart = segEnd;
                continue;
            }

            // 横倒しラン用: 欧文ベースラインを列の中心へずらす量（アセントは
            // +u 側、ディセントは -u 側へ倒れるので、その中点を 0 に合わせる）
            float baselineU = 0.0f;
            float ascent = 0.0f, descent = 0.0f;
            if (!upright) {
                const FontFaceMetrics& fm = face->getFaceMetrics();
                const float upem = (fm.unitsPerEm > 0) ? fm.unitsPerEm : 1000.0f;
                ascent = fm.ascenderUnits / upem * size;      // 正
                descent = -fm.descenderUnits / upem * size;   // 正
                baselineU = (descent - ascent) * 0.5f;
            }

            // HarfBuzz のクラスタ単位でまとめる（組版層はこの単位で扱う）
            for (unsigned int i = 0; i < numGlyphs;) {
                unsigned int j = i;
                while (j + 1 < numGlyphs && info[j + 1].cluster == info[i].cluster) {
                    ++j;
                }

                ShapedCluster sc;
                sc.glyphStart = static_cast<uint32_t>(result.glyphs.size());
                sc.charStart = info[i].cluster;
                sc.charEnd = (j + 1 < numGlyphs) ? info[j + 1].cluster
                                                 : static_cast<size_t>(segEnd);
                sc.origin = pen;
                sc.upright = upright;
                {
                    size_t cpLen = 1;
                    sc.charClass = getCharClass(codePointAt(text, sc.charStart, cpLen));
                }

                float clusterAdvance = 0.0f;
                for (unsigned int k = i; k <= j; ++k) {
                    GlyphInfo g;
                    g.glyphId = info[k].codepoint;
                    g.font = face;
                    g.charIndex = info[k].cluster;
                    g.fakery = run.fakedFont.fakery;

                    float adv;
                    if (upright) {
                        // TTB: HarfBuzz は縦原点を差し引いた offset を返すので、
                        // グリフは通常どおり（水平原点で）ペン位置に置けばよい。
                        // y は上向き正なので描画先（y-down）では符号を反転する。
                        g.x = HBFixedToFloat(pos[k].x_offset);
                        g.y = pen - HBFixedToFloat(pos[k].y_offset);
                        adv = -HBFixedToFloat(pos[k].y_advance);   // 下向きが負
                    } else {
                        // 横倒し: 横組みで組んでから列へ 90 度倒す。
                        //   ローカル (lx, ly) → 列 (u, v) = (-ly, lx)
                        const float ly = -HBFixedToFloat(pos[k].y_offset);
                        g.x = -ly + baselineU;
                        g.y = pen + HBFixedToFloat(pos[k].x_offset);
                        g.rotation = kSidewaysRotation;
                        adv = HBFixedToFloat(pos[k].x_advance);
                    }

                    clusterAdvance += adv;
                    g.advance = adv;
                    pen += adv;
                    result.glyphs.push_back(g);
                }

                // 字間はクラスタの区切りに入れる。Phase 2 の組版層は
                // ShapedCluster::advance（字間を含まない素の送り）を使う。
                if (letterSpacing != 0.0f && !result.glyphs.empty()) {
                    result.glyphs.back().advance += letterSpacing;
                    pen += letterSpacing;
                }

                sc.glyphCount = static_cast<uint32_t>(result.glyphs.size()) - sc.glyphStart;
                sc.advance = clusterAdvance;
                result.clusters.push_back(sc);

                i = j + 1;
            }

            if (upright) {
                // 正立の列幅はベタ組みの 1em
                minU = std::min(minU, -size * 0.5f);
                maxU = std::max(maxU, size * 0.5f);
            } else {
                minU = std::min(minU, baselineU - descent);
                maxU = std::max(maxU, baselineU + ascent);
            }
            haveExtent = true;

            segStart = segEnd;
        }
    }

    result.advance = pen;
    if (haveExtent) {
        result.extentLeft = minU;
        result.extentRight = maxU;
    }
    return result;
}

} // namespace richtext::vertical
