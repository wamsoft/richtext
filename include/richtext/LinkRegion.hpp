#ifndef RICHTEXT_LINK_REGION_HPP
#define RICHTEXT_LINK_REGION_HPP

#include <vector>
#include <string>

namespace richtext {

/**
 * リンク矩形領域（1行分）
 */
struct LinkRect {
    float left = 0, top = 0, right = 0, bottom = 0;

    bool contains(float x, float y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }

    float width() const { return right - left; }
    float height() const { return bottom - top; }
};

/**
 * リンク領域情報
 *
 * 1つのリンク指定（%lname; 〜 %l;）に対応する。
 * 複数行にまたがる場合、rects に行ごとの矩形が格納される。
 */
struct LinkRegion {
    std::string name;                   ///< リンク名
    std::vector<int> charIndices;       ///< 所属する文字インデックス
    std::vector<LinkRect> rects;        ///< 行ごとの矩形領域

    /**
     * 指定座標がこのリンク領域に含まれるか
     */
    bool contains(float x, float y) const {
        for (const auto& r : rects) {
            if (r.contains(x, y)) return true;
        }
        return false;
    }
};

} // namespace richtext

#endif // RICHTEXT_LINK_REGION_HPP
