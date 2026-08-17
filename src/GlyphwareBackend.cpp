/**
 * GlyphwareBackend.cpp
 *
 * glyphware を直接リンクする FontBackend 実装
 */

#include "richtext/GlyphwareBackend.hpp"

#ifdef RICHTEXT_USE_GLYPHWARE

#include <glyphware/Blob.h>
#include <glyphware/Face.h>

namespace richtext {

namespace {

/**
 * richtext のバイト列バッファを glyphware の FontBlob として見せる
 * （コピーせず shared_ptr を保持するだけ）
 */
class VectorFontBlob : public glyphware::FontBlob {
public:
    explicit VectorFontBlob(std::shared_ptr<std::vector<uint8_t>> bytes)
        : bytes_(std::move(bytes)) {}
    const std::uint8_t* data() const noexcept override { return bytes_->data(); }
    std::size_t size() const noexcept override { return bytes_->size(); }

private:
    std::shared_ptr<std::vector<uint8_t>> bytes_;
};

/**
 * richtext の FontOutlineSink を glyphware の OutlineSink に橋渡し
 */
class SinkAdapter : public glyphware::OutlineSink {
public:
    explicit SinkAdapter(FontOutlineSink& sink) : sink_(sink) {}
    void moveTo(float x, float y) override { sink_.moveTo(x, y); }
    void lineTo(float x, float y) override { sink_.lineTo(x, y); }
    void quadTo(float cx, float cy, float x, float y) override {
        sink_.quadTo(cx, cy, x, y);
    }
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override {
        sink_.cubicTo(c1x, c1y, c2x, c2y, x, y);
    }
    void close() override { sink_.close(); }

private:
    FontOutlineSink& sink_;
};

int toPixelSize(float size) {
    int px = static_cast<int>(size + 0.5f);
    return px > 0 ? px : 1;
}

//------------------------------------------------------------------------------
// FontBackendFace 実装
//------------------------------------------------------------------------------

class GlyphwareFace : public FontBackendFace {
public:
    GlyphwareFace(std::shared_ptr<glyphware::Face> face, std::string key)
        : face_(std::move(face))
        , key_(std::move(key))
        , family_(face_->descriptor().family)
        , style_(face_->descriptor().subfamily) {}

    const std::string& familyName() const override { return family_; }
    const std::string& styleName() const override { return style_; }
    bool isColorFont() const override { return face_->descriptor().color; }
    bool isScalable() const override { return face_->descriptor().scalable; }

    FontFaceMetrics faceMetrics() const override {
        const glyphware::LineMetrics lm = face_->lineMetrics();
        FontFaceMetrics m;
        m.unitsPerEm = lm.unitsPerEm > 0 ? lm.unitsPerEm : 1000.0f;
        m.ascenderUnits = lm.ascenderUnits;
        m.descenderUnits = lm.descenderUnits;
        m.heightUnits = lm.heightUnits;
        return m;
    }

    const void* fontData() const override { return face_->data(); }
    size_t fontDataSize() const override { return face_->size(); }
    int faceIndex() const override { return face_->faceIndex(); }

    bool getGlyphMetrics(uint32_t glyphId, float pixelSize, bool bold, bool italic,
                         bool unhinted, FontGlyphMetrics& out) const override {
        if (!face_->setPixelSize(toPixelSize(pixelSize))) return false;
        glyphware::GlyphMetrics m;
        if (!face_->glyphMetrics(glyphId, m, bold, italic,
                                 unhinted ? glyphware::Hinting::Unhinted
                                          : glyphware::Hinting::Hinted)) {
            return false;
        }
        assign(m, out);
        return true;
    }

    bool getGlyphMetricsUnscaled(uint32_t glyphId, bool bold, bool italic,
                                 FontGlyphMetrics& out) const override {
        glyphware::GlyphMetrics m;
        if (!face_->glyphMetricsUnscaled(glyphId, m, bold, italic)) return false;
        assign(m, out);
        return true;
    }

    bool getGlyphOutline(uint32_t glyphId, bool bold, bool italic,
                         FontOutlineSink& sink) const override {
        SinkAdapter adapter(sink);
        return face_->glyphOutline(glyphId, adapter, bold, italic);
    }

    bool getGlyphBitmap(uint32_t glyphId, float pixelSize, bool color,
                        bool bold, bool italic, FontGlyphBitmapView& out) override {
        if (!face_->setPixelSize(toPixelSize(pixelSize))) return false;
        glyphware::GlyphBitmap bmp;
        if (!face_->glyphBitmap(glyphId, color, bmp, bold, italic)) return false;
        if (bmp.width <= 0 || bmp.rows <= 0 || !bmp.buffer) return false;
        out.format = bmp.format == glyphware::BitmapFormat::BGRA
                         ? FontBitmapFormat::BGRA
                         : FontBitmapFormat::Gray;
        out.left = bmp.left;
        out.top = bmp.top;
        out.width = bmp.width;
        out.rows = bmp.rows;
        out.pitch = bmp.pitch;
        out.buffer = bmp.buffer;
        return true;
    }

    uint32_t getGlyphIndex(char32_t codepoint) const override {
        return face_->glyphIndex(codepoint);
    }

    bool isVariableFont() const override { return !face_->descriptor().axes.empty(); }

    std::vector<FontVarCoord> getAxes() const override {
        std::vector<FontVarCoord> out;
        for (const auto& a : face_->descriptor().axes) {
            out.push_back({a.tag, a.defaultValue});
        }
        return out;
    }

    bool setVariations(const std::vector<FontVarCoord>& coords) override {
        std::vector<glyphware::VarCoord> gw;
        gw.reserve(coords.size());
        for (const auto& c : coords) gw.push_back({c.tag, c.value});
        return face_->setVariations(gw);
    }

    bool getAxisRange(uint32_t tag, float& minValue, float& defaultValue,
                      float& maxValue) const override {
        return face_->axisRange(tag, minValue, defaultValue, maxValue);
    }

    bool getColorLayers(uint32_t glyphId, float pixelSize,
                        std::vector<FontColorLayer>& out,
                        FontColorGlyphBox* box) override {
        if (!face_->setPixelSize(toPixelSize(pixelSize))) return false;
        std::vector<glyphware::ColorLayer> layers;
        glyphware::ColorGlyphBox gwBox;
        if (!face_->colorLayers(glyphId, layers, box ? &gwBox : nullptr)) return false;

        out.clear();
        out.reserve(layers.size());
        for (const auto& l : layers) {
            FontColorLayer dst;
            dst.glyphId = l.gid;
            for (int i = 0; i < 6; ++i) dst.transform[i] = l.transform[i];
            dst.paint.kind = l.paint.kind == glyphware::PaintKind::LinearGradient
                                 ? FontPaintKind::LinearGradient
                             : l.paint.kind == glyphware::PaintKind::RadialGradient
                                 ? FontPaintKind::RadialGradient
                                 : FontPaintKind::Solid;
            dst.paint.r = l.paint.r;
            dst.paint.g = l.paint.g;
            dst.paint.b = l.paint.b;
            dst.paint.a = l.paint.a;
            dst.paint.x0 = l.paint.x0;
            dst.paint.y0 = l.paint.y0;
            dst.paint.x1 = l.paint.x1;
            dst.paint.y1 = l.paint.y1;
            dst.paint.r0 = l.paint.r0;
            dst.paint.r1 = l.paint.r1;
            dst.paint.stops.reserve(l.paint.stops.size());
            for (const auto& s : l.paint.stops) {
                dst.paint.stops.push_back({s.offset, s.r, s.g, s.b, s.a});
            }
            out.push_back(std::move(dst));
        }
        if (box) {
            box->xMin = gwBox.xMin;
            box->yMin = gwBox.yMin;
            box->xMax = gwBox.xMax;
            box->yMax = gwBox.yMax;
            box->valid = gwBox.valid;
        }
        return true;
    }

private:
    static void assign(const glyphware::GlyphMetrics& src, FontGlyphMetrics& dst) {
        dst.advanceX = src.advanceX;
        dst.advanceY = src.advanceY;
        dst.bearingX = src.bearingX;
        dst.bearingY = src.bearingY;
        dst.width = src.width;
        dst.height = src.height;
    }

    std::shared_ptr<glyphware::Face> face_;
    std::string key_;
    std::string family_;
    std::string style_;
};

} // namespace

//------------------------------------------------------------------------------
// GlyphwareFontBackend
//------------------------------------------------------------------------------

GlyphwareFontBackend::GlyphwareFontBackend(ByteLoader loader)
    : loader_(std::move(loader)) {}

GlyphwareFontBackend::~GlyphwareFontBackend() = default;

std::shared_ptr<FontBackendFace> GlyphwareFontBackend::openFace(const std::string& key,
                                                                int index) {
    if (!loader_) return nullptr;
    auto bytes = loader_(key);
    if (!bytes || bytes->empty()) return nullptr;

    auto blob = std::make_shared<VectorFontBlob>(std::move(bytes));
    auto face = glyphware::Face::open(std::move(blob), key, index);
    if (!face) return nullptr;

    return std::make_shared<GlyphwareFace>(std::move(face), key);
}

} // namespace richtext

#endif // RICHTEXT_USE_GLYPHWARE
