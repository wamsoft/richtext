/**
 * FontFace.cpp
 * 
 * FreeType と minikin の橋渡しを行うフォントフェイスクラス
 */

#include "richtext/FontFace.hpp"
#include "richtext/FontManager.hpp"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <vector>
#include <functional>

#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#include FT_COLOR_H

// minikin types
#include <minikin/MinikinPaint.h>
#include <minikin/MinikinRect.h>
#include <minikin/MinikinExtent.h>

namespace richtext {

namespace {

// ユニークID生成
static int32_t sUniqueIdCounter = 0;

// FreeType 定数
constexpr FT_Int32 LOAD_FLAG =
    FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;

// FreeType の座標を float に変換
constexpr float FTPosToFloat(FT_Pos x) { return x / 64.0f; }

// float を FreeType の 26.6 固定小数点に変換
constexpr FT_F26Dot6 FloatToF26Dot6(float x) {
    return static_cast<FT_F26Dot6>(x * 64);
}

// グリフのロード
FT_Error loadGlyph(uint32_t glyphId, float size, FT_Face face,
                   FT_Int32 flags = LOAD_FLAG) {
    FT_Error err;
    if (FT_HAS_FIXED_SIZES(face)) {
        // 固定サイズビットマップフォント（CBDT等）
        err = FT_Select_Size(face, 0);
    } else {
        // COLRv1 等のアウトラインベースフォントは FT_Set_Pixel_Sizes を使用
        err = FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size + 0.5f));
    }
    if (err != 0) {
        return err;
    }

    err = FT_Load_Glyph(face, glyphId, flags);
    if (err != 0) {
        return err;
    }

    return FT_Err_Ok;
}

// ファイル全体をメモリに読み込む
std::vector<uint8_t> readWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return {};
    }
    
    return buffer;
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// FontFace 実装
// ----------------------------------------------------------------------------

FontFace::FontFace(const std::string& path, int index)
    : minikin::MinikinFont(sUniqueIdCounter++)
    , fontPath_(path)
    , fontIndex_(index)
    , ftFace_(nullptr)
    , fontData_(nullptr)
    , fontDataSize_(0)
{
    // フォントファイルをメモリに読み込む
    fontDataBuffer_ = readWholeFile(path);
    if (fontDataBuffer_.empty()) {
        throw std::runtime_error("Failed to read font file: " + path);
    }
    
    fontData_ = fontDataBuffer_.data();
    fontDataSize_ = fontDataBuffer_.size();
    
    // FreeType でフォントを開く
    FT_Library ftLib = FontManager::instance().getFTLibrary();
    if (!ftLib) {
        throw std::runtime_error("FreeType library not initialized");
    }
    
    FT_Open_Args args;
    args.flags = FT_OPEN_MEMORY;
    args.memory_base = static_cast<const FT_Byte*>(fontData_);
    args.memory_size = fontDataSize_;
    
    FT_Error err = FT_Open_Face(ftLib, &args, index, &ftFace_);
    if (err != 0) {
        throw std::runtime_error("Failed to open font face: " + path);
    }
}

FontFace::~FontFace() {
    releaseFace();
}

void FontFace::releaseFace() {
    if (ftFace_) {
        FT_Done_Face(ftFace_);
        ftFace_ = nullptr;
    }
}

float FontFace::GetHorizontalAdvance(uint32_t glyphId,
                                     const minikin::MinikinPaint& paint,
                                     const minikin::FontFakery& /*fakery*/) const {
    if (!ftFace_) return 0.0f;
    
    FT_Int32 flags;
    bool hasColor = FT_HAS_COLOR(ftFace_);
    
    if (hasColor) {
        flags = FT_LOAD_COLOR | FT_LOAD_DEFAULT;
    } else {
        flags = LOAD_FLAG;
    }
    
    FT_Error err = loadGlyph(glyphId, paint.size, ftFace_, flags);
    if (err != 0) {
        return 0.0f;
    }
    
    float advance = FTPosToFloat(ftFace_->glyph->advance.x);
    
    // カラービットマップフォントはスケーリングが必要
    if (hasColor && FT_HAS_FIXED_SIZES(ftFace_) && ftFace_->num_fixed_sizes > 0) {
        float fixedSize = ftFace_->available_sizes[0].height;
        if (fixedSize > 0) {
            float scale = paint.size / fixedSize;
            advance *= scale;
        }
    }
    
    return advance;
}

void FontFace::GetBounds(minikin::MinikinRect* bounds, uint32_t glyphId,
                         const minikin::MinikinPaint& paint,
                         const minikin::FontFakery& /*fakery*/) const {
    if (!ftFace_ || !bounds) return;
    
    FT_Error err = loadGlyph(glyphId, paint.size, ftFace_);
    if (err != 0) {
        bounds->mLeft = bounds->mTop = bounds->mRight = bounds->mBottom = 0;
        return;
    }
    
    if (ftFace_->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_BBox bbox;
        FT_Outline_Get_CBox(&ftFace_->glyph->outline, &bbox);
        
        bounds->mLeft = FTPosToFloat(bbox.xMin);
        bounds->mTop = FTPosToFloat(bbox.yMax);
        bounds->mRight = FTPosToFloat(bbox.xMax);
        bounds->mBottom = FTPosToFloat(bbox.yMin);
    } else {
        // ビットマップの場合（CBDT等）
        // ストライクのネイティブ座標なので要求サイズにスケール
        FT_GlyphSlot slot = ftFace_->glyph;
        float scale = 1.0f;
        if (FT_HAS_FIXED_SIZES(ftFace_) && ftFace_->num_fixed_sizes > 0) {
            float fixedSize = ftFace_->available_sizes[0].height;
            if (fixedSize > 0) {
                scale = paint.size / fixedSize;
            }
        }
        bounds->mLeft = static_cast<float>(slot->bitmap_left) * scale;
        bounds->mTop = static_cast<float>(slot->bitmap_top) * scale;
        bounds->mRight = bounds->mLeft + slot->bitmap.width * scale;
        bounds->mBottom = bounds->mTop - slot->bitmap.rows * scale;
    }
}

void FontFace::GetFontExtent(minikin::MinikinExtent* extent,
                             const minikin::MinikinPaint& paint,
                             const minikin::FontFakery& /*fakery*/) const {
    if (!ftFace_ || !extent) return;
    
    float upem = ftFace_->units_per_EM;
    if (upem <= 0) upem = 1000;
    
    extent->ascent = -static_cast<float>(ftFace_->ascender) * paint.size / upem;
    extent->descent = -static_cast<float>(ftFace_->descender) * paint.size / upem;
}

bool FontFace::isColorGlyph(uint32_t /*glyphId*/) const {
    if (!ftFace_) return false;
    return FT_HAS_COLOR(ftFace_) != 0;
}

bool FontFace::getGlyphPath(uint32_t glyphId, float size,
                            std::vector<tvg::PathCommand>& commands,
                            std::vector<tvg::Point>& points) const {
    if (!ftFace_) return false;
    
    // カラー絵文字はパス取得不可
    if (isColorGlyph(glyphId)) {
        return false;
    }
    
    FT_Error err = loadGlyph(glyphId, size, ftFace_, LOAD_FLAG);
    if (err != 0) {
        return false;
    }

    if (ftFace_->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        return false;
    }

    commands.clear();
    points.clear();

    // FreeType アウトラインを thorvg パスに変換
    outlineToPath(&ftFace_->glyph->outline, 1.0f, commands, points);
    
    return true;
}

bool FontFace::getGlyphBitmap(uint32_t glyphId, float size,
                              GlyphBitmap& bitmap) const {
    if (!ftFace_) return false;

    bool hasColor = FT_HAS_COLOR(ftFace_);

    // Step 1: FT_LOAD_COLOR のみでロード（RENDER なし）
    FT_Int32 flags = hasColor
        ? FT_LOAD_COLOR
        : FT_LOAD_DEFAULT;

    FT_Error err = loadGlyph(glyphId, size, ftFace_, flags);
    if (err != 0) {
        return false;
    }

    FT_GlyphSlot slot = ftFace_->glyph;

    // Step 2: アウトラインの場合はレンダリング
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        err = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
        if (err != 0) {
            return false;
        }
    }
    
    FT_Bitmap& ftBitmap = slot->bitmap;

    //------------------------------------------------------------------
    // COLRv0 レイヤーフォールバック
    // FreeType が COLRv1 を自動合成できない場合、COLRv0 レイヤーを
    // 個別レンダリング＋合成して RGBA ビットマップを生成する
    //------------------------------------------------------------------
    if (ftBitmap.width == 0 && ftBitmap.rows == 0 && hasColor) {
        if (renderCOLRv1Glyph(glyphId, size, bitmap)) {
            return true;
        }
        return false;
    }

    //------------------------------------------------------------------
    // 通常パス: CBDT / grayscale
    //------------------------------------------------------------------
    bitmap.width = ftBitmap.width;
    bitmap.height = ftBitmap.rows;
    bitmap.bearingX = slot->bitmap_left;
    bitmap.bearingY = slot->bitmap_top;
    // GetHorizontalAdvance と同じ基準でスケール計算
    if (FT_HAS_FIXED_SIZES(ftFace_) && ftFace_->num_fixed_sizes > 0) {
        bitmap.strikeHeight = ftFace_->available_sizes[0].height;
    }

    // ピクセルデータをコピー
    if (ftBitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        // カラービットマップ (BGRA -> RGBA)
        bitmap.data.resize(ftBitmap.width * ftBitmap.rows * 4);
        for (unsigned int y = 0; y < ftBitmap.rows; ++y) {
            for (unsigned int x = 0; x < ftBitmap.width; ++x) {
                size_t srcIdx = y * ftBitmap.pitch + x * 4;
                size_t dstIdx = (y * ftBitmap.width + x) * 4;
                bitmap.data[dstIdx + 0] = ftBitmap.buffer[srcIdx + 2]; // R
                bitmap.data[dstIdx + 1] = ftBitmap.buffer[srcIdx + 1]; // G
                bitmap.data[dstIdx + 2] = ftBitmap.buffer[srcIdx + 0]; // B
                bitmap.data[dstIdx + 3] = ftBitmap.buffer[srcIdx + 3]; // A
            }
        }
    } else if (ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
        // グレースケール -> RGBA
        bitmap.data.resize(ftBitmap.width * ftBitmap.rows * 4);
        for (unsigned int y = 0; y < ftBitmap.rows; ++y) {
            for (unsigned int x = 0; x < ftBitmap.width; ++x) {
                size_t srcIdx = y * ftBitmap.pitch + x;
                size_t dstIdx = (y * ftBitmap.width + x) * 4;
                uint8_t alpha = ftBitmap.buffer[srcIdx];
                bitmap.data[dstIdx + 0] = 255;   // R
                bitmap.data[dstIdx + 1] = 255;   // G
                bitmap.data[dstIdx + 2] = 255;   // B
                bitmap.data[dstIdx + 3] = alpha; // A
            }
        }
    } else {
        return false;
    }

    return true;
}

void FontFace::outlineToPath(FT_Outline* outline, float scale,
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
