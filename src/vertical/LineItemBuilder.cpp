/**
 * LineItemBuilder.cpp
 *
 * シェイピング結果 → Box / Glue / Penalty 列
 *
 * インライン注記（ルビ・縦中横・圏点・割注・字取り）もここで解決する。
 * 注記を描画時の後処理にせず組版アイテムへ落とし込むのは、行分割・約物の
 * 詰め・両端揃えと矛盾させないため。
 */

#include "richtext/vertical/LineItemBuilder.hpp"

#include "richtext/TextLayout.hpp"
#include "richtext/vertical/LineBreaker.hpp"

#include <algorithm>
#include <cmath>

namespace richtext::vertical {

namespace {

//------------------------------------------------------------------------------
// 禁則とボディ
//------------------------------------------------------------------------------

/**
 * 2 つのクラスタの間で改行できるか
 *
 * JLReq の禁則。ここで false になる位置には Penalty(∞) を置き、
 * 続く Glue がブレーク点にならないようにする。
 */
bool canBreakBetween(CharClass before, CharClass after) {
    // 行末禁則: 始め括弧類・前置省略記号の直後では切らない
    if (isLineEndProhibited(before)) return false;
    // 行頭禁則: 終わり括弧類・句読点・中点類・小書きの仮名等の直前では切らない
    if (isLineStartProhibited(after)) return false;

    // 分離禁止文字（—— …… 等）は 2 つ並びを割らない
    if (before == CharClass::Inseparable && after == CharClass::Inseparable) {
        return false;
    }

    // 欧文単語の内部と連数字は割らない。欧文の切れ目は空白（Glue）だけ
    if (isWestern(before) && isWestern(after)) return false;
    // 省略記号は続く／先立つ数字と離さない
    if (before == CharClass::PrefixAbbr && isWestern(after)) return false;
    if (isWestern(before) && after == CharClass::PostfixAbbr) return false;

    return true;
}

/**
 * 詰めた仮想ボディの中でのグリフ位置
 *
 * シェイパーが返す送り（通常 1em）と、詰めたあとのボディ幅の差を、
 * 字面の寄りに応じて前後へ振り分ける。
 */
float bodyGlyphOffset(CharClass cls, float shapedAdvance, float bodyWidth) {
    const float slack = shapedAdvance - bodyWidth;
    if (slack <= 0.0f) return 0.0f;
    switch (getBodyAlign(cls)) {
        case BodyAlign::End:    return -slack;
        case BodyAlign::Center: return -slack * 0.5f;
        case BodyAlign::Start:
        case BodyAlign::Full:
        default:                return 0.0f;
    }
}

/// ルビがこの文字に掛かってよいか（JLReq: 掛けられるのは仮名・漢字）
bool canRubyOverhang(CharClass cls) {
    if (!isJapanese(cls)) return false;
    if (isHalfWidthPunctuation(cls)) return false;
    switch (cls) {
        case CharClass::Dividing:
        case CharClass::IdeographicSpace:
        case CharClass::Inseparable:
            return false;
        default:
            return true;
    }
}

//------------------------------------------------------------------------------
// ルビ
//------------------------------------------------------------------------------

struct RubyResult {
    std::vector<GlyphInfo> glyphs;   ///< v は親範囲の先頭からの相対
    float endGap = 0.0f;             ///< 親範囲の前後に入れる固定アキ
    float innerGap = 0.0f;           ///< 親 Box の間に入れる固定アキ
    float extentRight = 0.0f;        ///< 縦ベースラインからの右への張り出し
    bool valid = false;
};

/**
 * ルビ 1 個分の配置
 *
 * @param parentWidth 親文字列の長さ
 * @param parentCount 親文字（Box）の数
 * @param canHangBefore/After 前後の文字にルビを掛けてよいか
 */
RubyResult layoutRuby(const std::u16string& rubyText, const TextStyle& style,
                      float rubySize, float parentWidth, int parentCount,
                      bool canHangBefore, bool canHangAfter) {
    RubyResult out;
    if (rubyText.empty() || parentCount <= 0) return out;

    TextStyle rubyStyle = style;
    rubyStyle.fontSize = rubySize;
    rubyStyle.letterSpacing = 0.0f;

    const VerticalShaper::Result shaped =
            VerticalShaper::shape(rubyText, rubyStyle, TextOrientation::Mixed);
    if (shaped.clusters.empty()) return out;

    const float rubyLength = shaped.advance;
    const int n = static_cast<int>(shaped.clusters.size());

    // ルビの u 位置: 親のボディ右端に接して外側へ
    const float uShift = style.fontSize * 0.5f + rubySize * 0.5f;
    out.extentRight = style.fontSize * 0.5f + rubySize;

    float startV;
    std::vector<float> clusterV(n);

    if (rubyLength <= parentWidth) {
        // 中付き: ルビ文字の間と前後（前後は半分）へ均等に配分する
        const float extra = parentWidth - rubyLength;
        const float gap = extra / static_cast<float>(n);
        startV = gap * 0.5f;
        for (int i = 0; i < n; ++i) {
            clusterV[i] = startV + shaped.clusters[i].origin + gap * static_cast<float>(i);
        }
    } else {
        // ルビのほうが長い: まず前後の文字へ掛け、それでも足りない分だけ親を広げる
        const float overflow = rubyLength - parentWidth;
        const float hangBefore = canHangBefore ? std::min(overflow * 0.5f, rubySize) : 0.0f;
        const float hangAfter = canHangAfter ? std::min(overflow * 0.5f, rubySize) : 0.0f;
        const float rest = std::max(0.0f, overflow - hangBefore - hangAfter);

        if (rest > 0.0f) {
            // 親文字列の前後 1 : 文字間 2 の比で配る（中付きの配分）
            const float unit = rest / (2.0f * static_cast<float>(parentCount));
            out.endGap = unit;
            out.innerGap = unit * 2.0f;
        }

        startV = -hangBefore;
        for (int i = 0; i < n; ++i) {
            clusterV[i] = startV + shaped.clusters[i].origin;
        }
    }

    out.glyphs.reserve(shaped.glyphs.size());
    for (int i = 0; i < n; ++i) {
        const ShapedCluster& cluster = shaped.clusters[i];
        const float delta = clusterV[i] - cluster.origin;
        for (uint32_t g = 0; g < cluster.glyphCount; ++g) {
            GlyphInfo glyph = shaped.glyphs[cluster.glyphStart + g];
            glyph.x += uShift;
            glyph.y += delta;
            glyph.fontSize = rubySize;
            out.glyphs.push_back(glyph);
        }
    }
    out.valid = true;
    return out;
}

//------------------------------------------------------------------------------
// 縦中横・割注（Box 本体を差し替えるもの）
//------------------------------------------------------------------------------

struct CompositeResult {
    std::vector<GlyphInfo> glyphs;
    float width = 0.0f;
    bool valid = false;
};

/**
 * 縦中横 — 半角数字等を 1em 角に正立で収める
 *
 * 中身は横組みなので minikin の TextLayout でそのまま組み、1em に入らない
 * 分だけ scaleX で圧縮する。
 */
CompositeResult layoutTateChuYoko(const std::u16string& sub, const TextStyle& style) {
    CompositeResult out;
    if (sub.empty()) return out;

    TextLayout layout;
    layout.layout(sub, style);
    if (layout.getGlyphCount() == 0) return out;

    const float em = style.fontSize;
    const float runWidth = layout.getWidth();
    const float scale = (runWidth > em && runWidth > 0.0f) ? (em / runWidth) : 1.0f;

    // 1em 角の中で、欧文のアセント〜ディセントの帯が中央に来るようにする
    const float ascent = layout.getAscent();    // 負
    const float descent = layout.getDescent();  // 正
    const float baselineV = em * 0.5f - (ascent + descent) * 0.5f * scale;

    out.glyphs.reserve(layout.getGlyphCount());
    for (const GlyphInfo& src : layout.getGlyphs()) {
        GlyphInfo glyph = src;
        glyph.x = (src.x - runWidth * 0.5f) * scale;
        glyph.y = baselineV + src.y * scale;
        glyph.advance = src.advance * scale;
        glyph.scaleX = scale;
        glyph.fontSize = em;
        out.glyphs.push_back(glyph);
    }
    out.width = em;
    out.valid = true;
    return out;
}

/**
 * 割注 — 行内に 2 行の子ブロックを組む
 *
 * 子は本文の半分の文字サイズで組み、上下（縦組みでは右左）2 段に割る。
 * 段の長さは「全体の半分」から始めて、2 行に収まるまで少しずつ伸ばす。
 */
CompositeResult layoutWarichu(const std::u16string& content, const TextStyle& style,
                              float scale, const VerticalSpacingOptions& opts) {
    CompositeResult out;
    if (content.empty()) return out;

    TextStyle childStyle = style;
    childStyle.fontSize = style.fontSize * scale;

    const VerticalShaper::Result shaped =
            VerticalShaper::shape(content, childStyle, TextOrientation::Mixed);
    if (shaped.clusters.empty()) return out;

    VerticalSpacingOptions childOpts = opts;
    childOpts.hangingPunctuation = false;
    childOpts.letterSpacing = 0.0f;
    const std::vector<LineItem> items = buildLineItems(shaped, childStyle, {}, childOpts);

    float total = 0.0f;
    for (const LineItem& it : items) {
        if (it.isBox()) total += it.width;
        else if (it.isGlue()) total += it.natural;
    }

    LineBreakOptions breakOpts;
    breakOpts.strategy = LineBreakStrategy::Greedy;
    breakOpts.justify = false;

    std::vector<BreakLine> breaks;
    float target = std::max(childStyle.fontSize, total * 0.5f);
    for (int attempt = 0; attempt < 32; ++attempt) {
        breaks = LineBreaker::breakLines(items, target, breakOpts);
        if (breaks.size() <= 2) break;
        target *= 1.08f;
    }
    if (breaks.empty()) return out;

    // vertical-rl では先に来る段が右（u が正）側
    const float quarter = childStyle.fontSize * 0.5f;

    for (size_t li = 0; li < breaks.size() && li < 2; ++li) {
        const BreakLine& br = breaks[li];
        const float uShift = (li == 0) ? quarter : -quarter;

        float v = 0.0f;
        for (uint32_t i = br.itemStart; i < br.itemEnd; ++i) {
            const LineItem& item = items[i];
            if (item.isGlue()) {
                v += item.natural;
                continue;
            }
            if (!item.isBox()) continue;

            if (item.ownGlyphs) {
                for (GlyphInfo glyph : item.glyphs) {
                    glyph.x += uShift;
                    glyph.y += v;
                    out.glyphs.push_back(glyph);
                }
            } else {
                const ShapedCluster& cluster = shaped.clusters[item.clusterIndex];
                const float delta = (v + item.glyphOffset) - cluster.origin;
                for (uint32_t g = 0; g < cluster.glyphCount; ++g) {
                    GlyphInfo glyph = shaped.glyphs[cluster.glyphStart + g];
                    glyph.x += uShift;
                    glyph.y += delta;
                    glyph.fontSize = childStyle.fontSize;
                    out.glyphs.push_back(glyph);
                }
            }
            v += item.width;
        }
        out.width = std::max(out.width, br.naturalWidth);
    }

    out.valid = !out.glyphs.empty();
    return out;
}

//------------------------------------------------------------------------------
// 圏点
//------------------------------------------------------------------------------

/// 圏点 1 個分のグリフ（v は親 Box の先頭からの相対）
bool layoutEmphasisMark(EmphasisMark mark, const TextStyle& style, float markSize,
                        float parentWidth, std::vector<GlyphInfo>& out) {
    const char32_t cp = emphasisMarkCodePoint(mark);
    std::u16string text;
    if (cp <= 0xFFFF) {
        text += static_cast<char16_t>(cp);
    } else {
        const char32_t v = cp - 0x10000;
        text += static_cast<char16_t>(0xD800 | (v >> 10));
        text += static_cast<char16_t>(0xDC00 | (v & 0x3FF));
    }

    TextStyle markStyle = style;
    markStyle.fontSize = markSize;
    markStyle.letterSpacing = 0.0f;

    const VerticalShaper::Result shaped =
            VerticalShaper::shape(text, markStyle, TextOrientation::Upright);
    if (shaped.glyphs.empty()) return false;

    const float uShift = style.fontSize * 0.5f + markSize * 0.5f;
    const float vShift = (parentWidth - shaped.advance) * 0.5f;

    for (GlyphInfo glyph : shaped.glyphs) {
        glyph.x += uShift;
        glyph.y += vShift;
        glyph.fontSize = markSize;
        out.push_back(glyph);
    }
    return true;
}

//------------------------------------------------------------------------------
// 注記の解決
//------------------------------------------------------------------------------

struct ResolvedAnnotation {
    const InlineAnnotation* ann = nullptr;
    uint32_t clusterStart = 0;
    uint32_t clusterEnd = 0;   ///< exclusive
};

std::vector<ResolvedAnnotation> resolveAnnotations(
        const VerticalShaper::Result& shaped,
        const std::vector<InlineAnnotation>& annotations) {
    std::vector<ResolvedAnnotation> out;
    out.reserve(annotations.size());

    for (const InlineAnnotation& ann : annotations) {
        if (ann.end <= ann.start) continue;
        ResolvedAnnotation r;
        r.ann = &ann;
        bool found = false;
        for (uint32_t ci = 0; ci < shaped.clusters.size(); ++ci) {
            const ShapedCluster& c = shaped.clusters[ci];
            if (c.charStart >= ann.start && c.charStart < ann.end) {
                if (!found) {
                    r.clusterStart = ci;
                    found = true;
                }
                r.clusterEnd = ci + 1;
            }
        }
        if (found) out.push_back(r);
    }
    return out;
}

/// `|` 区切りでモノルビの各文字分を取り出す
std::vector<std::u16string> splitMonoRuby(const std::u16string& text) {
    std::vector<std::u16string> parts;
    size_t pos = 0;
    while (true) {
        const size_t bar = text.find(u'|', pos);
        if (bar == std::u16string::npos) {
            parts.push_back(text.substr(pos));
            break;
        }
        parts.push_back(text.substr(pos, bar - pos));
        pos = bar + 1;
    }
    return parts;
}

} // namespace

//------------------------------------------------------------------------------

std::vector<LineItem> buildLineItems(const VerticalShaper::Result& shaped,
                                     const TextStyle& style,
                                     const std::vector<InlineAnnotation>& annotations,
                                     const VerticalSpacingOptions& opts) {
    std::vector<LineItem> items;
    if (shaped.clusters.empty()) {
        return items;
    }
    items.reserve(shaped.clusters.size() * 3 + 2);

    const float em = style.fontSize;
    const float letterSpacing = opts.letterSpacing * em;
    const uint32_t clusterCount = static_cast<uint32_t>(shaped.clusters.size());

    //--------------------------------------------------------------------------
    // 前処理: 注記をクラスタ単位の「ボディ幅・固定アキ・付随グリフ」へ落とす
    //--------------------------------------------------------------------------
    const std::vector<ResolvedAnnotation> resolved = resolveAnnotations(shaped, annotations);

    constexpr int kNone = -1;
    std::vector<int> composite(clusterCount, kNone);    // 縦中横・割注（範囲先頭に記録）
    std::vector<uint8_t> skipped(clusterCount, 0);      // 合成 Box に吸収されたクラスタ
    std::vector<uint8_t> noBreak(clusterCount, 0);      // このクラスタの手前で切らない
    std::vector<float> bodyWidths(clusterCount, 0.0f);
    std::vector<float> gapBefore(clusterCount + 1, 0.0f);   // クラスタ ci の直前の固定アキ
    std::vector<std::vector<GlyphInfo>> attached(clusterCount);
    std::vector<float> extentRight(clusterCount, 0.0f);
    std::vector<CompositeResult> composites(resolved.size());

    // 1) 合成 Box（縦中横・割注）を先に組む。ボディ幅がここで決まる
    for (int ri = 0; ri < static_cast<int>(resolved.size()); ++ri) {
        const ResolvedAnnotation& r = resolved[ri];
        if (r.ann->type != AnnotationType::TateChuYoko &&
            r.ann->type != AnnotationType::Warichu) {
            continue;
        }
        const size_t s = shaped.clusters[r.clusterStart].charStart;
        const size_t e = shaped.clusters[r.clusterEnd - 1].charEnd;
        if (r.ann->type == AnnotationType::TateChuYoko) {
            composites[ri] = layoutTateChuYoko(shaped.sourceText.substr(s, e - s), style);
        } else {
            // 割注の内容は明示指定が無ければ範囲の本文をそのまま使う
            const std::u16string content =
                    r.ann->text.empty() ? shaped.sourceText.substr(s, e - s) : r.ann->text;
            composites[ri] = layoutWarichu(content, style, r.ann->scale, opts);
        }
        if (!composites[ri].valid) continue;

        composite[r.clusterStart] = ri;
        for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) {
            skipped[ci] = 1;
        }
    }

    // 2) 仮想ボディ幅
    for (uint32_t ci = 0; ci < clusterCount; ++ci) {
        if (skipped[ci]) continue;
        if (composite[ci] != kNone) {
            bodyWidths[ci] = composites[composite[ci]].width;
            continue;
        }
        const CharClass cls = shaped.clusters[ci].charClass;
        float bodyEm = opts.punctuationSpacing ? getBodyWidth(cls) : 0.0f;
        if (!opts.punctuationSpacing && isJapanese(cls)) bodyEm = 1.0f;
        float w = (bodyEm > 0.0f) ? bodyEm * em : shaped.clusters[ci].advance;
        if (bodyEm > 0.0f && shaped.clusters[ci].advance > 0.0f) {
            w = std::min(w, shaped.clusters[ci].advance);
        }
        bodyWidths[ci] = w;
    }

    // 3) ルビ・圏点・字取り
    for (const ResolvedAnnotation& r : resolved) {
        // 範囲内の親 Box（合成に吸収されたものは数えない）
        std::vector<uint32_t> parents;
        float parentWidth = 0.0f;
        for (uint32_t k = r.clusterStart; k < r.clusterEnd; ++k) {
            if (skipped[k]) continue;
            parents.push_back(k);
            parentWidth += bodyWidths[k];
        }
        if (parents.empty()) continue;
        const int parentCount = static_cast<int>(parents.size());

        switch (r.ann->type) {
            case AnnotationType::Ruby: {
                const float rubySize = em * r.ann->scale;
                std::vector<std::u16string> parts;
                if (r.ann->rubyMode == RubyMode::Mono) {
                    parts = splitMonoRuby(r.ann->text);
                }

                if (r.ann->rubyMode == RubyMode::Mono &&
                    static_cast<int>(parts.size()) == parentCount) {
                    // モノルビ: 親文字 1 文字ずつに掛ける
                    for (int pi = 0; pi < parentCount; ++pi) {
                        const uint32_t k = parents[pi];
                        RubyResult rr = layoutRuby(parts[pi], style, rubySize,
                                                   bodyWidths[k], 1, false, false);
                        if (!rr.valid) continue;
                        attached[k].insert(attached[k].end(), rr.glyphs.begin(),
                                           rr.glyphs.end());
                        extentRight[k] = std::max(extentRight[k], rr.extentRight);
                        if (rr.endGap > 0.0f) {
                            gapBefore[k] += rr.endGap;
                            gapBefore[k + 1] += rr.endGap;
                        }
                    }
                } else {
                    // グループルビ: 範囲全体に 1 つ。範囲内では改行しない
                    const bool hangBefore =
                            (r.clusterStart > 0) &&
                            canRubyOverhang(shaped.clusters[r.clusterStart - 1].charClass);
                    const bool hangAfter =
                            (r.clusterEnd < clusterCount) &&
                            canRubyOverhang(shaped.clusters[r.clusterEnd].charClass);

                    RubyResult rr = layoutRuby(r.ann->text, style, rubySize, parentWidth,
                                               parentCount, hangBefore, hangAfter);
                    if (!rr.valid) break;

                    const uint32_t head = parents.front();
                    attached[head].insert(attached[head].end(), rr.glyphs.begin(),
                                          rr.glyphs.end());
                    extentRight[head] = std::max(extentRight[head], rr.extentRight);

                    if (rr.endGap > 0.0f) {
                        gapBefore[r.clusterStart] += rr.endGap;
                        gapBefore[r.clusterEnd] += rr.endGap;
                    }
                    if (rr.innerGap > 0.0f) {
                        for (int pi = 1; pi < parentCount; ++pi) {
                            gapBefore[parents[pi]] += rr.innerGap;
                        }
                    }
                    for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) {
                        noBreak[ci] = 1;
                    }
                }
                break;
            }

            case AnnotationType::Emphasis: {
                const float markSize = em * r.ann->scale;
                for (uint32_t k : parents) {
                    if (layoutEmphasisMark(r.ann->mark, style, markSize, bodyWidths[k],
                                           attached[k])) {
                        extentRight[k] = std::max(extentRight[k], em * 0.5f + markSize);
                    }
                }
                break;
            }

            case AnnotationType::Jidori: {
                const float extra = r.ann->jidoriEm * em - parentWidth;
                if (extra <= 0.0f) break;
                if (parentCount == 1) {
                    gapBefore[r.clusterStart] += extra * 0.5f;
                    gapBefore[r.clusterEnd] += extra * 0.5f;
                } else {
                    const float gap = extra / static_cast<float>(parentCount - 1);
                    for (int pi = 1; pi < parentCount; ++pi) {
                        gapBefore[parents[pi]] += gap;
                    }
                }
                for (uint32_t ci = r.clusterStart + 1; ci < r.clusterEnd; ++ci) {
                    noBreak[ci] = 1;
                }
                break;
            }

            case AnnotationType::TateChuYoko:
            case AnnotationType::Warichu:
                break;   // 1) で処理済み
        }
    }

    //--------------------------------------------------------------------------
    // 本体
    //--------------------------------------------------------------------------
    bool prevWasBox = false;
    CharClass prevClass = CharClass::Unknown;
    float prevBoxWidth = 0.0f;

    for (uint32_t ci = 0; ci < clusterCount; ++ci) {
        if (skipped[ci]) continue;

        const ShapedCluster& cluster = shaped.clusters[ci];
        const CharClass cls = cluster.charClass;
        const int compIdx = composite[ci];

        // --- 欧文間隔は Box ではなく Glue にする（そこが唯一の欧文の切れ目） ---
        if (cls == CharClass::Space && compIdx == kNone) {
            const float w = cluster.advance;
            items.push_back(LineItem::gluePx(w, w * 0.5f, w / 3.0f, cluster.charStart));
            prevWasBox = false;
            prevClass = cls;
            continue;
        }

        // 合成 Box は和字として扱う（前後のアキと禁則の判定用）
        const CharClass spacingClass =
                (compIdx != kNone) ? CharClass::Ideographic : cls;
        const float boxWidth = bodyWidths[ci];

        // --- クラスタ間のアキと禁則 ---
        if (prevWasBox) {
            const bool latinBoundary =
                    (isJapanese(prevClass) && isWestern(spacingClass)) ||
                    (isWestern(prevClass) && isJapanese(spacingClass));
            GlueSpec spec = getSpacing(prevClass, spacingClass);
            if (latinBoundary ? !opts.latinGap : !opts.punctuationSpacing) {
                spec = GlueSpec{};
            }
            float natural = spec.natural * em + letterSpacing + gapBefore[ci];
            float stretch = spec.stretch * em;
            float shrink = spec.shrink * em;

            const bool breakable =
                    canBreakBetween(prevClass, spacingClass) && !noBreak[ci];

            // ぶら下げ: 句読点の直後は「幅が負の Penalty」で切る。ブレークすると
            // 句読点 1 文字分が行長から引かれる＝版面外へ出る。
            // Penalty を挟むと直後の Glue はブレーク点でなくなるので
            // （TeX の「Glue の直前が Box のときだけ切れる」規則）、
            // ここでの切り方はぶら下げに一本化される。
            if (breakable && opts.hangingPunctuation && isHangable(prevClass)) {
                items.push_back(
                        LineItem::penaltyItem(0.0f, -prevBoxWidth, cluster.charStart));
            } else if (!breakable) {
                items.push_back(
                        LineItem::penaltyItem(kInfinitePenalty, 0.0f, cluster.charStart));
            }

            if (breakable || natural != 0.0f || stretch != 0.0f || shrink != 0.0f) {
                items.push_back(
                        LineItem::gluePx(natural, stretch, shrink, cluster.charStart));
            }
        }

        // --- Box ---
        LineItem box = LineItem::box(boxWidth, ci, cluster.charStart);
        if (compIdx != kNone) {
            box.glyphs = composites[compIdx].glyphs;
            box.ownGlyphs = true;
        } else {
            box.glyphOffset = bodyGlyphOffset(cls, cluster.advance, boxWidth);
        }
        if (!attached[ci].empty()) {
            box.glyphs.insert(box.glyphs.end(), attached[ci].begin(), attached[ci].end());
            box.extentRight = extentRight[ci];
        }
        // 合成・付随グリフは別のテキストを組んだものなので、逐次表示のために
        // 親 Box の文字位置へ揃える
        for (GlyphInfo& glyph : box.glyphs) {
            glyph.charIndex = cluster.charStart;
        }

        items.push_back(std::move(box));

        prevWasBox = true;
        prevClass = spacingClass;
        prevBoxWidth = boxWidth;
    }

    // --- 段落の終端（TeX と同じ形） ---
    // 無限に伸びる Glue で最終行を伸ばさないようにし、強制ブレークで閉じる
    const size_t tailIndex = shaped.clusters.back().charEnd;
    items.push_back(LineItem::penaltyItem(kInfinitePenalty, 0.0f, tailIndex));
    items.push_back(LineItem::gluePx(0.0f, 1.0e6f, 0.0f, tailIndex));
    items.push_back(LineItem::penaltyItem(kForcedBreakPenalty, 0.0f, tailIndex));

    return items;
}

} // namespace richtext::vertical
