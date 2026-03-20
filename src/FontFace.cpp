/**
 * FontFace.cpp
 * 
 * FreeType と minikin の橋渡しを行うフォントフェイスクラス
 */

#include "richtext/FontFace.hpp"
#include "richtext/FontManager.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H

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
    const FT_F26Dot6 scale = FloatToF26Dot6(size);
    const FT_UInt dpi = 72;

    FT_Error err;
    if (FT_HAS_FIXED_SIZES(face)) {
        // 固定サイズビットマップフォント（絵文字等）
        err = FT_Select_Size(face, 0);
    } else {
        err = FT_Set_Char_Size(face, scale, scale, dpi, dpi);
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
        // ビットマップの場合
        FT_GlyphSlot slot = ftFace_->glyph;
        bounds->mLeft = static_cast<float>(slot->bitmap_left);
        bounds->mTop = static_cast<float>(slot->bitmap_top);
        bounds->mRight = bounds->mLeft + slot->bitmap.width;
        bounds->mBottom = bounds->mTop - slot->bitmap.rows;
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
    
    FT_Int32 flags = FT_HAS_COLOR(ftFace_) 
        ? (FT_LOAD_COLOR | FT_LOAD_DEFAULT)
        : FT_LOAD_DEFAULT;
    
    FT_Error err = loadGlyph(glyphId, size, ftFace_, flags);
    if (err != 0) {
        return false;
    }
    
    FT_GlyphSlot slot = ftFace_->glyph;
    
    // アウトラインの場合はレンダリング
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        err = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
        if (err != 0) {
            return false;
        }
    }
    
    FT_Bitmap& ftBitmap = slot->bitmap;
    
    bitmap.width = ftBitmap.width;
    bitmap.height = ftBitmap.rows;
    bitmap.bearingX = slot->bitmap_left;
    bitmap.bearingY = slot->bitmap_top;
    
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
                
                // 次の点がコントロールポイントなら、スキップしない
                if (nextTag != FT_CURVE_TAG_CONIC) {
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

} // namespace richtext
