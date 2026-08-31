/**
 * VerticalLayout.cpp
 *
 * 縦組み 1 行のレイアウト
 */

#include "richtext/vertical/VerticalLayout.hpp"

#include <algorithm>

namespace richtext::vertical {

void VerticalLayout::layout(const std::u16string& text, const TextStyle& style,
                            TextOrientation orientation) {
    text_ = text;
    style_ = style;
    orientation_ = orientation;
    doLayout();
}

void VerticalLayout::layout(std::u16string&& text, const TextStyle& style,
                            TextOrientation orientation) {
    text_ = std::move(text);
    style_ = style;
    orientation_ = orientation;
    doLayout();
}

void VerticalLayout::doLayout() {
    glyphs_.clear();
    length_ = 0.0f;
    extentLeft_ = 0.0f;
    extentRight_ = 0.0f;

    if (text_.empty() || !style_.fontCollection) {
        return;
    }

    VerticalShaper::Result result = VerticalShaper::shape(text_, style_, orientation_);
    glyphs_ = std::move(result.glyphs);
    length_ = result.advance;
    extentLeft_ = result.extentLeft;
    extentRight_ = result.extentRight;
}

size_t VerticalLayout::getCharCount() const {
    if (glyphs_.empty()) return 0;
    std::vector<size_t> chars;
    chars.reserve(glyphs_.size());
    for (const auto& g : glyphs_) chars.push_back(g.charIndex);
    std::sort(chars.begin(), chars.end());
    chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
    return chars.size();
}

} // namespace richtext::vertical
