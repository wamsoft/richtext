#ifndef RICHTEXT_GLYPHWARE_BACKEND_HPP
#define RICHTEXT_GLYPHWARE_BACKEND_HPP

#ifdef RICHTEXT_USE_GLYPHWARE

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "richtext/FontBackend.hpp"

namespace richtext {

/**
 * glyphware を直接リンクする既定バックエンド
 *
 * フォントバイト列は呼び出し側が渡すローダー（FontManager の
 * FontDataLoader と同じもの）から取得する。glyphware::Registry は使わず
 * `Face::open()` を直接呼ぶ: richtext はバリアブルフォントの軸違いを
 * 別 face として扱うため、キー単位で face を共有されると困るため。
 *
 * 本体（吉里吉里Z）と face を共有したい場合はこのバックエンドではなく、
 * 本体の FontServiceIntf を包んだバックエンドを FontManager に注入する。
 */
class GlyphwareFontBackend : public FontBackend {
public:
    /// フォント名 → バイト列。失敗時は空の shared_ptr
    using ByteLoader =
        std::function<std::shared_ptr<std::vector<uint8_t>>(const std::string&)>;

    explicit GlyphwareFontBackend(ByteLoader loader);
    ~GlyphwareFontBackend() override;

    std::shared_ptr<FontBackendFace> openFace(const std::string& key,
                                              int index) override;

private:
    ByteLoader loader_;
};

} // namespace richtext

#endif // RICHTEXT_USE_GLYPHWARE
#endif // RICHTEXT_GLYPHWARE_BACKEND_HPP
