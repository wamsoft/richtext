/**
 * FontFace.cpp
 *
 * FontBackend（glyphware / ホスト注入フォントエンジン）と minikin の橋渡しを
 * 行うフォントフェイスクラス
 */

#include "richtext/FontFace.hpp"
#include "richtext/FontManager.hpp"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <functional>

// COLRv1 のペイントグラフ走査だけは FreeType の API に直接依存する
// （バックエンドが FT_Face を公開している場合のみ動作する）
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_COLOR_H

// minikin types
#include <minikin/MinikinPaint.h>
#include <minikin/MinikinRect.h>
#include <minikin/MinikinExtent.h>

namespace richtext {

namespace {

// ユニークID生成
static int32_t sUniqueIdCounter = 0;

// バリアブルフォント軸タグ
constexpr uint32_t kWdthTag = 0x77647468;  // 'wdth'

// FreeType の座標を float に変換（COLRv1 経路で使用）
constexpr float FTPosToFloat(FT_Pos x) { return x / 64.0f; }

/**
 * バックエンドのアウトライン（フォントユニット・y-up）を thorvg パスに変換する
 * sink。scale でピクセルに変換し、Y 軸を反転する。
 */
class TvgPathSink : public FontOutlineSink {
public:
    TvgPathSink(float scale,
                std::vector<tvg::PathCommand>& commands,
                std::vector<tvg::Point>& points)
        : scale_(scale), commands_(commands), points_(points) {}

    void moveTo(float x, float y) override {
        commands_.push_back(tvg::PathCommand::MoveTo);
        cur_ = point(x, y);
        points_.push_back(cur_);
    }

    void lineTo(float x, float y) override {
        commands_.push_back(tvg::PathCommand::LineTo);
        cur_ = point(x, y);
        points_.push_back(cur_);
    }

    void quadTo(float cx, float cy, float x, float y) override {
        // 2次ベジェ → 3次ベジェ
        const tvg::Point c = point(cx, cy);
        const tvg::Point e = point(x, y);
        const tvg::Point c1{cur_.x + (2.0f / 3.0f) * (c.x - cur_.x),
                            cur_.y + (2.0f / 3.0f) * (c.y - cur_.y)};
        const tvg::Point c2{e.x + (2.0f / 3.0f) * (c.x - e.x),
                            e.y + (2.0f / 3.0f) * (c.y - e.y)};
        commands_.push_back(tvg::PathCommand::CubicTo);
        points_.push_back(c1);
        points_.push_back(c2);
        points_.push_back(e);
        cur_ = e;
    }

    void cubicTo(float c1x, float c1y, float c2x, float c2y,
                 float x, float y) override {
        commands_.push_back(tvg::PathCommand::CubicTo);
        points_.push_back(point(c1x, c1y));
        points_.push_back(point(c2x, c2y));
        cur_ = point(x, y);
        points_.push_back(cur_);
    }

    void close() override {
        commands_.push_back(tvg::PathCommand::Close);
    }

private:
    tvg::Point point(float x, float y) const {
        return tvg::Point{x * scale_, -y * scale_};  // Y軸反転
    }

    float scale_;
    std::vector<tvg::PathCommand>& commands_;
    std::vector<tvg::Point>& points_;
    tvg::Point cur_{0.0f, 0.0f};
};

/**
 * FreeType アウトラインを thorvg パスに変換（COLRv1 経路専用）
 */
void outlineToPath(FT_Outline* outline,
                   float scale,
                   std::vector<tvg::PathCommand>& commands,
                   std::vector<tvg::Point>& points) {
    if (!outline) return;

    int contourStart = 0;

    for (int c = 0; c < outline->n_contours; ++c) {
        int contourEnd = outline->contours[c];
        bool firstPoint = true;

        for (int p = contourStart; p <= contourEnd; ++p) {
            FT_Vector& pt = outline->points[p];
            char tag = outline->tags[p] & 0x03;

            float x = FTPosToFloat(pt.x) * scale;
            float y = -FTPosToFloat(pt.y) * scale;  // Y軸反転

            if (firstPoint) {
                commands.push_back(tvg::PathCommand::MoveTo);
                points.push_back({x, y});
                firstPoint = false;
            } else if (tag == FT_CURVE_TAG_ON) {
                // 直線
                commands.push_back(tvg::PathCommand::LineTo);
                points.push_back({x, y});
            } else if (tag == FT_CURVE_TAG_CONIC) {
                // 2次ベジェ -> 3次ベジェに変換
                int nextIdx = (p + 1 > contourEnd) ? contourStart : p + 1;
                FT_Vector& nextPt = outline->points[nextIdx];
                char nextTag = outline->tags[nextIdx] & 0x03;

                float nextX = FTPosToFloat(nextPt.x) * scale;
                float nextY = -FTPosToFloat(nextPt.y) * scale;

                // 次の点もコントロールポイントの場合、中点を終点とする
                if (nextTag == FT_CURVE_TAG_CONIC) {
                    nextX = (x + nextX) / 2.0f;
                    nextY = (y + nextY) / 2.0f;
                }

                // 直前の点を取得
                float prevX = points.back().x;
                float prevY = points.back().y;

                // 2次ベジェを3次ベジェに変換
                float cp1x = prevX + (2.0f / 3.0f) * (x - prevX);
                float cp1y = prevY + (2.0f / 3.0f) * (y - prevY);
                float cp2x = nextX + (2.0f / 3.0f) * (x - nextX);
                float cp2y = nextY + (2.0f / 3.0f) * (y - nextY);

                commands.push_back(tvg::PathCommand::CubicTo);
                points.push_back({cp1x, cp1y});
                points.push_back({cp2x, cp2y});
                points.push_back({nextX, nextY});

                // 次の点がオンカーブ点なら、その点は消費済みなのでスキップ
                // ただし nextIdx がラップアラウンドした場合はスキップしない
                if (nextTag != FT_CURVE_TAG_CONIC && nextIdx > p) {
                    p = nextIdx - 1;  // ループで+1されるため
                }
            } else if (tag == FT_CURVE_TAG_CUBIC) {
                // 3次ベジェ（2つのコントロールポイント）
                int nextIdx = (p + 1 > contourEnd) ? contourStart : p + 1;
                int endIdx = (p + 2 > contourEnd) ? contourStart : p + 2;

                FT_Vector& cp2 = outline->points[nextIdx];
                FT_Vector& endPt = outline->points[endIdx];

                float cp2x = FTPosToFloat(cp2.x) * scale;
                float cp2y = -FTPosToFloat(cp2.y) * scale;
                float endX = FTPosToFloat(endPt.x) * scale;
                float endY = -FTPosToFloat(endPt.y) * scale;

                commands.push_back(tvg::PathCommand::CubicTo);
                points.push_back({x, y});
                points.push_back({cp2x, cp2y});
                points.push_back({endX, endY});

                p += 2;  // 2つ進める
            }
        }

        // 輪郭を閉じる
        commands.push_back(tvg::PathCommand::Close);

        contourStart = contourEnd + 1;
    }
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// FontFace 実装
// ----------------------------------------------------------------------------

FontFace::FontFace(const std::string& name,
                   std::shared_ptr<FontBackendFace> face,
                   int index)
    : minikin::MinikinFont(sUniqueIdCounter++)
    , fontName_(name)
    , fontIndex_(index)
    , face_(std::move(face))
{
    if (!face_) {
        throw std::runtime_error("Null font backend face for: " + name);
    }

    familyName_ = face_->familyName();
    styleName_ = face_->styleName();
    faceMetrics_ = face_->faceMetrics();

    // COLRv1 走査用（公開されないバックエンドでは nullptr）
    ftFace_ = static_cast<FT_Face>(face_->nativeFTFace());

    // バリアブルフォント軸
    const std::vector<FontVarCoord> axes = face_->getAxes();
    isVariable_ = !axes.empty();
    axes_.reserve(axes.size());
    for (const auto& axis : axes) {
        axes_.emplace_back(static_cast<minikin::AxisTag>(axis.tag), axis.value);
    }
    hasWdthAxis_ = face_->getAxisRange(kWdthTag, wdthMin_, wdthDefault_, wdthMax_);
}

FontFace::~FontFace() {
    releaseFace();
}

void FontFace::releaseFace() {
    ftFace_ = nullptr;
    face_.reset();
}

FT_Face FontFace::getFTFace() const {
    return ftFace_;
}

const void* FontFace::GetFontData() const {
    return face_ ? face_->fontData() : nullptr;
}

size_t FontFace::GetFontSize() const {
    return face_ ? face_->fontDataSize() : 0;
}

void FontFace::setVariations(const std::vector<minikin::FontVariation>& variations) {
    if (!face_ || !isVariable_) return;

    // axes_ を更新
    axes_ = variations;

    // 未指定の軸は既定値へ戻す（従来の FreeType 実装と同じ挙動）
    std::vector<FontVarCoord> coords = face_->getAxes();
    for (const auto& var : variations) {
        bool found = false;
        for (auto& c : coords) {
            if (c.tag == var.axisTag) {
                c.value = var.value;
                found = true;
                break;
            }
        }
        if (!found) {
            coords.push_back({static_cast<uint32_t>(var.axisTag), var.value});
        }
    }
    face_->setVariations(coords);
}

bool FontFace::getWidthAxisRange(float& minWidth, float& maxWidth) const {
    if (!hasWdthAxis_) return false;
    minWidth = wdthMin_;
    maxWidth = wdthMax_;
    return true;
}

void FontFace::applyWidth(float width) const {
    if (!face_ || !hasWdthAxis_) return;
    const float clamped = std::clamp(width, wdthMin_, wdthMax_);
    face_->setVariations({{kWdthTag, clamped}});
}

float FontFace::GetHorizontalAdvance(uint32_t glyphId,
                                     const minikin::MinikinPaint& paint,
                                     const minikin::FontFakery& /*fakery*/) const {
    if (!face_) return 0.0f;

    // fontWidth が 100% 以外の場合、wdth 軸を設定してからグリフをロード
    float fakeScaleX = 1.0f;
    if (paint.fontWidth != 100.0f) {
        if (hasWdthAxis_) {
            float clampedWidth = std::clamp(paint.fontWidth, wdthMin_, wdthMax_);
            applyWidth(clampedWidth);
            fakeScaleX = paint.fontWidth / clampedWidth;
        } else {
            fakeScaleX = paint.fontWidth / 100.0f;
        }
    }

    // レイアウト用なのでヒンティング無し（アドバンスが整数に丸まらないように）
    FontGlyphMetrics m;
    if (!face_->getGlyphMetrics(glyphId, paint.size, false, false, true, m)) {
        return 0.0f;
    }

    // 固定サイズのカラービットマップフォントのスケーリングはバックエンドが行う
    return m.advanceX * fakeScaleX;
}

void FontFace::GetBounds(minikin::MinikinRect* bounds, uint32_t glyphId,
                         const minikin::MinikinPaint& paint,
                         const minikin::FontFakery& /*fakery*/) const {
    if (!face_ || !bounds) return;

    FontGlyphMetrics m;
    if (!face_->getGlyphMetrics(glyphId, paint.size, false, false, true, m)) {
        bounds->mLeft = bounds->mTop = bounds->mRight = bounds->mBottom = 0;
        return;
    }

    bounds->mLeft = m.bearingX;
    bounds->mTop = m.bearingY;
    bounds->mRight = m.bearingX + m.width;
    bounds->mBottom = m.bearingY - m.height;
}

void FontFace::GetFontExtent(minikin::MinikinExtent* extent,
                             const minikin::MinikinPaint& paint,
                             const minikin::FontFakery& /*fakery*/) const {
    if (!face_ || !extent) return;

    float upem = faceMetrics_.unitsPerEm;
    if (upem <= 0) upem = 1000.0f;

    extent->ascent = -faceMetrics_.ascenderUnits * paint.size / upem;
    extent->descent = -faceMetrics_.descenderUnits * paint.size / upem;
}

bool FontFace::isColorGlyph(uint32_t /*glyphId*/) const {
    return face_ && face_->isColorFont();
}

bool FontFace::getGlyphPath(uint32_t glyphId, float size,
                            std::vector<tvg::PathCommand>& commands,
                            std::vector<tvg::Point>& points) const {
    if (!face_) return false;

    // カラー絵文字はパス取得不可
    if (isColorGlyph(glyphId)) {
        return false;
    }

    float upem = faceMetrics_.unitsPerEm;
    if (upem <= 0) upem = 1000.0f;

    commands.clear();
    points.clear();

    // アウトラインはフォントユニットで返るので要求サイズへスケールする
    TvgPathSink sink(size / upem, commands, points);
    return face_->getGlyphOutline(glyphId, false, false, sink);
}

bool FontFace::getGlyphBitmap(uint32_t glyphId, float size,
                              GlyphBitmap& bitmap) const {
    if (!face_) return false;

    const bool hasColor = face_->isColorFont();

    FontGlyphBitmapView view;
    if (!face_->getGlyphBitmap(glyphId, size, hasColor, false, false, view)) {
        // FreeType が合成できない COLRv1 はペイントグラフを自前走査する
        if (hasColor) {
            return renderCOLRv1Glyph(glyphId, size, bitmap);
        }
        return false;
    }

    bitmap.width = view.width;
    bitmap.height = view.rows;
    bitmap.bearingX = view.left;
    bitmap.bearingY = view.top;
    // バックエンドが要求サイズへスケール済みなので、描画側での再スケールは不要
    bitmap.strikeHeight = 0.0f;

    const size_t pixels = static_cast<size_t>(view.width) * view.rows;
    bitmap.data.resize(pixels * 4);

    if (view.format == FontBitmapFormat::BGRA) {
        // カラービットマップ (BGRA -> RGBA)
        for (int y = 0; y < view.rows; ++y) {
            const uint8_t* src = view.buffer + static_cast<ptrdiff_t>(view.pitch) * y;
            uint8_t* dst = bitmap.data.data() + static_cast<size_t>(y) * view.width * 4;
            for (int x = 0; x < view.width; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];  // R
                dst[x * 4 + 1] = src[x * 4 + 1];  // G
                dst[x * 4 + 2] = src[x * 4 + 0];  // B
                dst[x * 4 + 3] = src[x * 4 + 3];  // A
            }
        }
    } else {
        // グレースケール -> RGBA（白 + カバレッジ）
        for (int y = 0; y < view.rows; ++y) {
            const uint8_t* src = view.buffer + static_cast<ptrdiff_t>(view.pitch) * y;
            uint8_t* dst = bitmap.data.data() + static_cast<size_t>(y) * view.width * 4;
            for (int x = 0; x < view.width; ++x) {
                dst[x * 4 + 0] = 255;
                dst[x * 4 + 1] = 255;
                dst[x * 4 + 2] = 255;
                dst[x * 4 + 3] = src[x];
            }
        }
    }

    return true;
}

//==============================================================================
// COLRv1 ペイントグラフレンダラ
//==============================================================================

bool FontFace::renderCOLRv1Glyph(uint32_t glyphId, float size,
                                  GlyphBitmap& bitmap) const {
    if (!ftFace_) return false;

    // サイズ設定（COLRv1 はアウトラインベースなので必須）
    FT_Set_Pixel_Sizes(ftFace_, 0, static_cast<FT_UInt>(size + 0.5f));

    // ルートペイント取得（ルートトランスフォーム込み）
    FT_OpaquePaint rootPaint = { NULL, 0 };
    if (!FT_Get_Color_Glyph_Paint(ftFace_, glyphId,
                                   FT_COLOR_INCLUDE_ROOT_TRANSFORM, &rootPaint)) {
        return false;
    }

    // ClipBox でバウンディングボックス取得
    FT_ClipBox clipBox;
    int bmpW, bmpH;
    int originX, originY;

    if (FT_Get_Color_Glyph_ClipBox(ftFace_, glyphId, &clipBox)) {
        // ClipBox は 26.6 固定小数点
        int x0 = clipBox.bottom_left.x >> 6;
        int y0 = -(clipBox.top_left.y >> 6);    // Y反転
        int x1 = (clipBox.top_right.x + 63) >> 6;
        int y1 = -(clipBox.bottom_right.y >> 6); // Y反転 (bottom_right.y < top_left.y)
        if (y0 > y1) std::swap(y0, y1);
        if (x0 > x1) std::swap(x0, x1);
        bmpW = x1 - x0;
        bmpH = y1 - y0;
        originX = x0;
        originY = y0;
    } else {
        // ClipBox がなければフォントメトリクスから推定
        float upem = static_cast<float>(ftFace_->units_per_EM);
        if (upem <= 0) upem = 1000;
        float scale = size / upem;
        bmpW = static_cast<int>(std::ceil(size * 1.2f));
        bmpH = static_cast<int>(std::ceil(size * 1.2f));
        originX = 0;
        originY = -static_cast<int>(std::ceil(ftFace_->ascender * scale));
    }

    if (bmpW <= 0 || bmpH <= 0) return false;
    // 安全上限
    if (bmpW > 1024 || bmpH > 1024) return false;

    // thorvg SwCanvas で一時描画バッファ作成
    std::vector<uint32_t> buffer(bmpW * bmpH, 0);
    auto canvas = tvg::SwCanvas::gen();
    canvas->target(buffer.data(), bmpW, bmpW, bmpH, tvg::ColorSpace::ARGB8888);

    // カラーパレット取得
    FT_Color* palette = nullptr;
    FT_Palette_Select(ftFace_, 0, &palette);

    // 16.16 固定小数点→float
    auto fixed16_16 = [](FT_Fixed v) -> float { return v / 65536.0f; };

    // パレットから RGBA 色を取得
    auto resolveColor = [&](const FT_ColorIndex& ci) -> uint32_t {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (palette && ci.palette_index != 0xFFFF) {
            FT_Color c = palette[ci.palette_index];
            r = c.red; g = c.green; b = c.blue; a = c.alpha;
        }
        // alpha に F2Dot14 の alpha を乗算
        float alphaScale = ci.alpha / 16384.0f;  // F2Dot14
        a = static_cast<uint8_t>(std::clamp(a * alphaScale, 0.0f, 255.0f));
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               static_cast<uint32_t>(b);
    };

    // ColorLine → thorvg gradient color stops
    auto applyColorStops = [&](tvg::Fill* fill, FT_ColorLine& colorline) {
        std::vector<tvg::Fill::ColorStop> stops;
        FT_ColorStop colorStop;
        while (FT_Get_Colorline_Stops(ftFace_, &colorStop,
                                       &colorline.color_stop_iterator)) {
            uint32_t c = resolveColor(colorStop.color);
            tvg::Fill::ColorStop stop;
            stop.offset = std::clamp(fixed16_16(colorStop.stop_offset), 0.0f, 1.0f);
            stop.r = (c >> 16) & 0xFF;
            stop.g = (c >> 8) & 0xFF;
            stop.b = c & 0xFF;
            stop.a = (c >> 24) & 0xFF;
            stops.push_back(stop);
        }
        if (!stops.empty()) {
            fill->colorStops(stops.data(), stops.size());
        }
    };

    // ペイントグラフの再帰走査
    // 戻り値: 描画された tvg::Shape (所有権は canvas に移譲済み)
    // currentClip: 現在の PaintGlyph のアウトラインで生成した Shape
    std::function<void(FT_OpaquePaint, tvg::Matrix*)> traversePaint;

    traversePaint = [&](FT_OpaquePaint opaquePaint, tvg::Matrix* parentMatrix) {
        FT_COLR_Paint paint;
        if (!FT_Get_Paint(ftFace_, opaquePaint, &paint)) return;

        switch (paint.format) {
        case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
            // 複数レイヤーを SRC_OVER で合成
            FT_OpaquePaint layerPaint = { NULL, 0 };
            while (FT_Get_Paint_Layers(ftFace_, &paint.u.colr_layers.layer_iterator,
                                        &layerPaint)) {
                traversePaint(layerPaint, parentMatrix);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_GLYPH: {
            // グリフアウトラインをクリップ/形状として使用
            // まずグリフのアウトラインを取得
            FT_UInt gid = paint.u.glyph.glyphID;
            FT_Load_Glyph(ftFace_, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP);

            if (ftFace_->glyph->format == FT_GLYPH_FORMAT_OUTLINE &&
                ftFace_->glyph->outline.n_contours > 0) {
                // アウトラインを thorvg パスに変換
                std::vector<tvg::PathCommand> cmds;
                std::vector<tvg::Point> pts;
                // FT_LOAD_NO_SCALE なので座標はフォント単位(FT_Pos)
                // outlineToPath は FTPosToFloat (÷64) を行うので、×64 でキャンセル
                outlineToPath(&ftFace_->glyph->outline, 64.0f, cmds, pts);

                // 座標はデザイン単位のまま。parentMatrix (ルートトランスフォーム含む) が
                // ピクセル変換を行い、origin 減算は最終トランスフォームに含める

                // 子ペイント（Solid/Gradient）を走査して塗り情報を取得
                // 子ペイントは shape のフィルとして適用
                FT_COLR_Paint childPaint;
                if (FT_Get_Paint(ftFace_, paint.u.glyph.paint, &childPaint)) {
                    auto shape = tvg::Shape::gen();
                    shape->appendPath(cmds.data(), cmds.size(), pts.data(), pts.size());

                    // parentMatrix でデザイン単位→ピクセル変換後、origin を引く
                    tvg::Matrix finalM = parentMatrix
                        ? tvg::Matrix{
                            parentMatrix->e11, parentMatrix->e12,
                            parentMatrix->e13 - static_cast<float>(originX),
                            parentMatrix->e21, parentMatrix->e22,
                            parentMatrix->e23 - static_cast<float>(originY),
                            0, 0, 1}
                        : tvg::Matrix{1, 0, -static_cast<float>(originX),
                                      0, 1, -static_cast<float>(originY),
                                      0, 0, 1};
                    shape->transform(finalM);

                    if (childPaint.format == FT_COLR_PAINTFORMAT_SOLID) {
                        uint32_t c = resolveColor(childPaint.u.solid.color);
                        shape->fill((c >> 16) & 0xFF, (c >> 8) & 0xFF,
                                    c & 0xFF, (c >> 24) & 0xFF);
                    } else if (childPaint.format == FT_COLR_PAINTFORMAT_LINEAR_GRADIENT) {
                        auto& lg = childPaint.u.linear_gradient;
                        auto* grad = tvg::LinearGradient::gen();
                        float x0 = fixed16_16(lg.p0.x) - originX;
                        float y0 = -fixed16_16(lg.p0.y) - originY;
                        float x1 = fixed16_16(lg.p1.x) - originX;
                        float y1 = -fixed16_16(lg.p1.y) - originY;
                        grad->linear(x0, y0, x1, y1);
                        applyColorStops(grad, lg.colorline);
                        shape->fill(grad);
                    } else if (childPaint.format == FT_COLR_PAINTFORMAT_RADIAL_GRADIENT) {
                        auto& rg = childPaint.u.radial_gradient;
                        auto* grad = tvg::RadialGradient::gen();
                        float cx = fixed16_16(rg.c1.x) - originX;
                        float cy = -fixed16_16(rg.c1.y) - originY;
                        float r  = fixed16_16(rg.r1);
                        float fx = fixed16_16(rg.c0.x) - originX;
                        float fy = -fixed16_16(rg.c0.y) - originY;
                        float fr = fixed16_16(rg.r0);
                        grad->radial(cx, cy, r, fx, fy, fr);
                        applyColorStops(grad, rg.colorline);
                        shape->fill(grad);
                    } else {
                        // 未対応の子ペイント → 再帰で処理
                        // shape にはデフォルト黒を設定
                        shape->fill(0, 0, 0, 255);
                    }

                    canvas->add(shape);
                }
            } else {
                // アウトラインがない場合は子ペイントを再帰走査
                traversePaint(paint.u.glyph.paint, parentMatrix);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_SOLID: {
            // 単色（通常は PaintGlyph の子として処理されるが、
            // トップレベルに来た場合は全体を塗る）
            break;
        }

        case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
            // 別のカラーグリフを参照
            FT_OpaquePaint subPaint = { NULL, 0 };
            if (FT_Get_Color_Glyph_Paint(ftFace_, paint.u.colr_glyph.glyphID,
                                          FT_COLOR_NO_ROOT_TRANSFORM, &subPaint)) {
                traversePaint(subPaint, parentMatrix);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_TRANSFORM: {
            auto& t = paint.u.transform.affine;
            tvg::Matrix m = {
                fixed16_16(t.xx), fixed16_16(t.xy), fixed16_16(t.dx),
                fixed16_16(t.yx), fixed16_16(t.yy), fixed16_16(t.dy),
                0, 0, 1
            };
            if (parentMatrix) {
                // 行列合成: parent * m
                tvg::Matrix combined = {
                    parentMatrix->e11 * m.e11 + parentMatrix->e12 * m.e21,
                    parentMatrix->e11 * m.e12 + parentMatrix->e12 * m.e22,
                    parentMatrix->e11 * m.e13 + parentMatrix->e12 * m.e23 + parentMatrix->e13,
                    parentMatrix->e21 * m.e11 + parentMatrix->e22 * m.e21,
                    parentMatrix->e21 * m.e12 + parentMatrix->e22 * m.e22,
                    parentMatrix->e21 * m.e13 + parentMatrix->e22 * m.e23 + parentMatrix->e23,
                    0, 0, 1
                };
                traversePaint(paint.u.transform.paint, &combined);
            } else {
                traversePaint(paint.u.transform.paint, &m);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_TRANSLATE: {
            float dx = fixed16_16(paint.u.translate.dx);
            float dy = fixed16_16(paint.u.translate.dy);
            tvg::Matrix m = { 1, 0, dx, 0, 1, dy, 0, 0, 1 };
            if (parentMatrix) {
                // 行列合成: parent * translate
                tvg::Matrix combined = {
                    parentMatrix->e11, parentMatrix->e12,
                    parentMatrix->e11 * dx + parentMatrix->e12 * dy + parentMatrix->e13,
                    parentMatrix->e21, parentMatrix->e22,
                    parentMatrix->e21 * dx + parentMatrix->e22 * dy + parentMatrix->e23,
                    0, 0, 1
                };
                traversePaint(paint.u.translate.paint, &combined);
            } else {
                traversePaint(paint.u.translate.paint, &m);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_SCALE: {
            auto& s = paint.u.scale;
            float sx = fixed16_16(s.scale_x);
            float sy = fixed16_16(s.scale_y);
            float cx = fixed16_16(s.center_x);
            float cy = fixed16_16(s.center_y);
            // M = translate(cx,cy) * scale(sx,sy) * translate(-cx,-cy)
            tvg::Matrix m = {
                sx, 0, cx * (1 - sx),
                0, sy, cy * (1 - sy),
                0, 0, 1
            };
            if (parentMatrix) {
                tvg::Matrix combined = {
                    parentMatrix->e11 * m.e11, parentMatrix->e12 * m.e22,
                    parentMatrix->e11 * m.e13 + parentMatrix->e12 * m.e23 + parentMatrix->e13,
                    parentMatrix->e21 * m.e11, parentMatrix->e22 * m.e22,
                    parentMatrix->e21 * m.e13 + parentMatrix->e22 * m.e23 + parentMatrix->e23,
                    0, 0, 1
                };
                traversePaint(s.paint, &combined);
            } else {
                traversePaint(s.paint, &m);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_ROTATE: {
            auto& rot = paint.u.rotate;
            // COLRv1 angle: turns (1.0 = 360°)
            float angle = fixed16_16(rot.angle) * 2.0f * 3.14159265f;
            float cosA = std::cos(angle), sinA = std::sin(angle);
            float cx = fixed16_16(rot.center_x);
            float cy = fixed16_16(rot.center_y);
            tvg::Matrix m = {
                cosA, -sinA, cx - cosA * cx + sinA * cy,
                sinA,  cosA, cy - sinA * cx - cosA * cy,
                0, 0, 1
            };
            if (parentMatrix) {
                tvg::Matrix combined = {
                    parentMatrix->e11 * m.e11 + parentMatrix->e12 * m.e21,
                    parentMatrix->e11 * m.e12 + parentMatrix->e12 * m.e22,
                    parentMatrix->e11 * m.e13 + parentMatrix->e12 * m.e23 + parentMatrix->e13,
                    parentMatrix->e21 * m.e11 + parentMatrix->e22 * m.e21,
                    parentMatrix->e21 * m.e12 + parentMatrix->e22 * m.e22,
                    parentMatrix->e21 * m.e13 + parentMatrix->e22 * m.e23 + parentMatrix->e23,
                    0, 0, 1
                };
                traversePaint(rot.paint, &combined);
            } else {
                traversePaint(rot.paint, &m);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_SKEW: {
            auto& sk = paint.u.skew;
            // COLRv1 angle: turns (1.0 = 360°)
            float xAngle = fixed16_16(sk.x_skew_angle) * 2.0f * 3.14159265f;
            float yAngle = fixed16_16(sk.y_skew_angle) * 2.0f * 3.14159265f;
            float tanX = std::tan(xAngle);
            float tanY = std::tan(yAngle);
            tvg::Matrix m = { 1, tanX, 0, tanY, 1, 0, 0, 0, 1 };
            if (parentMatrix) {
                tvg::Matrix combined = {
                    parentMatrix->e11 + parentMatrix->e12 * tanY,
                    parentMatrix->e11 * tanX + parentMatrix->e12,
                    parentMatrix->e13,
                    parentMatrix->e21 + parentMatrix->e22 * tanY,
                    parentMatrix->e21 * tanX + parentMatrix->e22,
                    parentMatrix->e23,
                    0, 0, 1
                };
                traversePaint(sk.paint, &combined);
            } else {
                traversePaint(sk.paint, &m);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_COMPOSITE: {
            // backdrop を先に描画、次に source
            traversePaint(paint.u.composite.backdrop_paint, parentMatrix);
            traversePaint(paint.u.composite.source_paint, parentMatrix);
            break;
        }

        default:
            // 未対応のペイントタイプ
            break;
        }
    };

    // ペイントグラフ走査開始
    traversePaint(rootPaint, nullptr);

    // レンダリング実行
    canvas->update();
    canvas->draw();
    canvas->sync();

    // ARGB バッファを RGBA に変換して GlyphBitmap に格納
    bitmap.width = bmpW;
    bitmap.height = bmpH;
    bitmap.bearingX = originX;
    bitmap.bearingY = -originY;
    bitmap.data.resize(bmpW * bmpH * 4);

    bool hasPixels = false;
    for (int i = 0; i < bmpW * bmpH; ++i) {
        uint32_t argb = buffer[i];
        uint8_t a = (argb >> 24) & 0xFF;
        uint8_t r = (argb >> 16) & 0xFF;
        uint8_t g = (argb >>  8) & 0xFF;
        uint8_t b =  argb        & 0xFF;
        bitmap.data[i * 4 + 0] = r;
        bitmap.data[i * 4 + 1] = g;
        bitmap.data[i * 4 + 2] = b;
        bitmap.data[i * 4 + 3] = a;
        if (a > 0) hasPixels = true;
    }

    return hasPixels;
}

} // namespace richtext
