/**
 * GlyphRenderer.cpp
 * 
 * グリフ単位の描画処理
 */

#include "richtext/GlyphRenderer.hpp"

namespace richtext {

//------------------------------------------------------------------------------
// コンストラクタ・デストラクタ
//------------------------------------------------------------------------------

GlyphRenderer::GlyphRenderer(tvg::Canvas* canvas)
    : canvas_(canvas)
{
}

GlyphRenderer::~GlyphRenderer() = default;

//------------------------------------------------------------------------------
// グリフ描画
//------------------------------------------------------------------------------

void GlyphRenderer::renderGlyph(const GlyphInfo& glyph,
                                float x, float y,
                                const TextStyle& style,
                                const Appearance& appearance) {
    if (!canvas_ || !glyph.font) {
        return;
    }
    
    // グリフ位置
    float glyphX = x + glyph.x;
    float glyphY = y + glyph.y;
    
    // FontFace からグリフパスまたはビットマップを取得
    const FontFace* font = glyph.font;
    
    // カラー絵文字判定
    if (font->isColorGlyph(glyph.glyphId)) {
        // ビットマップ描画
        GlyphBitmap bitmap;
        if (font->getGlyphBitmap(glyph.glyphId, style.fontSize, bitmap)) {
            // スケール計算（固定サイズビットマップの場合）
            float scale = 1.0f;
            if (bitmap.height > 0) {
                // ビットマップサイズとフォントサイズの比率でスケール
                float bitmapSize = static_cast<float>(std::max(bitmap.width, bitmap.height));
                scale = style.fontSize / bitmapSize;
            }
            renderBitmap(bitmap, glyphX + bitmap.bearingX * scale,
                        glyphY - bitmap.bearingY * scale, scale);
        }
    } else {
        // ベクターパス描画
        std::vector<tvg::PathCommand> commands;
        std::vector<tvg::Point> points;
        
        if (font->getGlyphPath(glyph.glyphId, style.fontSize, commands, points)) {
            renderPath(commands, points, glyphX, glyphY, appearance);
        }
    }
}

void GlyphRenderer::renderLayout(const TextLayout& layout,
                                 float x, float y,
                                 const Appearance& appearance) {
    const auto& glyphs = layout.getGlyphs();
    
    // スタイルを構築（レイアウトから取得する方法がないので最低限の情報を使う）
    // TODO: TextLayout に TextStyle 参照を保持させる
    TextStyle style;
    if (!glyphs.empty() && glyphs[0].font) {
        // フォントサイズは MinikinPaint から取得できないので
        // グリフの advance から推定するか、外部から渡す必要がある
        style.fontSize = 16.0f;  // デフォルト値（要改善）
    }
    
    for (const auto& glyph : glyphs) {
        renderGlyph(glyph, x, y, style, appearance);
    }
}

//------------------------------------------------------------------------------
// キャッシュ制御
//------------------------------------------------------------------------------

void GlyphRenderer::clearCache() {
    // TODO: キャッシュ実装
}

//------------------------------------------------------------------------------
// 内部実装
//------------------------------------------------------------------------------

void GlyphRenderer::renderPath(const std::vector<tvg::PathCommand>& commands,
                               const std::vector<tvg::Point>& points,
                               float x, float y,
                               const Appearance& appearance) {
    if (commands.empty() || !canvas_) {
        return;
    }
    
    // 各 DrawStyle に対して描画
    for (const auto& drawStyle : appearance.getStyles()) {
        tvg::Shape* shape = tvg::Shape::gen();
        if (!shape) continue;
        
        // パスを設定（コピーして座標をオフセット）
        std::vector<tvg::Point> offsetPoints = points;
        for (auto& pt : offsetPoints) {
            pt.x += x + drawStyle.offsetX;
            pt.y += y + drawStyle.offsetY;
        }
        
        shape->appendPath(commands.data(), static_cast<uint32_t>(commands.size()),
                         offsetPoints.data(), static_cast<uint32_t>(offsetPoints.size()));
        
        if (drawStyle.type == DrawStyle::Type::Fill) {
            // Fill
            shape->fill(drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
            shape->strokeWidth(0);
        } else {
            // Stroke
            shape->fill(0, 0, 0, 0);  // 透明なフィル
            shape->strokeWidth(drawStyle.strokeWidth);
            shape->strokeFill(drawStyle.r, drawStyle.g, drawStyle.b, drawStyle.a);
            shape->strokeCap(drawStyle.strokeCap);
            shape->strokeJoin(drawStyle.strokeJoin);
            shape->strokeMiterlimit(drawStyle.miterLimit);
        }
        
        canvas_->add(shape);
    }
}

void GlyphRenderer::renderBitmap(const GlyphBitmap& bitmap,
                                 float x, float y,
                                 float /*scale*/) {
    if (bitmap.data.empty() || !canvas_) {
        return;
    }
    
    // thorvg Picture を使用してビットマップを描画
    tvg::Picture* picture = tvg::Picture::gen();
    if (!picture) return;
    
    // RGBA データを ARGB (32bit) に変換
    // bitmap.data は RGBA8888 形式
    size_t pixelCount = static_cast<size_t>(bitmap.width) * bitmap.height;
    std::vector<uint32_t> argbData(pixelCount);
    
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t r = bitmap.data[i * 4 + 0];
        uint8_t g = bitmap.data[i * 4 + 1];
        uint8_t b = bitmap.data[i * 4 + 2];
        uint8_t a = bitmap.data[i * 4 + 3];
        // ARGB8888 format
        argbData[i] = (static_cast<uint32_t>(a) << 24) |
                      (static_cast<uint32_t>(r) << 16) |
                      (static_cast<uint32_t>(g) << 8) |
                      static_cast<uint32_t>(b);
    }
    
    tvg::Result result = picture->load(
        argbData.data(),
        static_cast<uint32_t>(bitmap.width),
        static_cast<uint32_t>(bitmap.height),
        tvg::ColorSpace::ARGB8888,
        true  // copy
    );
    
    if (result != tvg::Result::Success) {
        // add されていない Picture は thorvg が管理しないのでリークする可能性があるが、
        // thorvg の仕様上 protected destructor のため delete できない
        // ただし gen() で作成したものは実際には delete 可能のはず
        // 安全のため一旦何もしない（リークを許容）
        // TODO: thorvg の適切なリソース解放方法を調査
        return;
    }
    
    // スケールと位置を設定
    // picture->scale(scale);  // スケールは別途対応
    picture->translate(x, y);
    
    canvas_->add(picture);
}

} // namespace richtext
