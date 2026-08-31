#ifndef RICHTEXT_VERTICAL_LINE_BREAKER_HPP
#define RICHTEXT_VERTICAL_LINE_BREAKER_HPP

#include <cstdint>
#include <vector>

#include "richtext/vertical/LineItem.hpp"

/**
 * LineBreaker — 自前の行分割
 *
 * minikin の LineBreaker は使わない。伸縮グルーを扱えず、文字アドバンスの
 * 単純累積で行長を決めるため、追い込み／追い出しが表現できないため
 * （縦組み設計.md 4.3）。
 *
 * 合法なブレーク点は TeX と同じ規則（LineItem.hpp 参照）。行が確定すると
 * グルーの調整比 `ratio` が決まり、これを各グルーの伸び／縮みに掛けることで
 * 追い込み・追い出し・両端揃えがまとめて実現される。
 */
namespace richtext::vertical {

enum class LineBreakStrategy {
    Greedy,       ///< 各ブレーク候補で即断。画面のリアルタイム描画向け
    KnuthPlass,   ///< 段全体でデメリットを最小化。組版品質が要る用途向け
};

struct LineBreakOptions {
    LineBreakStrategy strategy = LineBreakStrategy::Greedy;

    /// 行末を揃える（グルーを伸縮させる）。false なら自然幅のまま
    bool justify = true;

    /// Knuth–Plass が許容するグルーの伸び率上限
    float tolerance = 3.0f;

    /// Knuth–Plass の行あたりのペナルティ（行数を増やしにくくする）
    float linePenalty = 10.0f;
};

/**
 * 確定した 1 行
 */
struct BreakLine {
    uint32_t itemStart = 0;    ///< 行を構成するアイテムの開始
    uint32_t itemEnd = 0;      ///< 同・終端（ブレーク位置。この位置は含まない）
    float ratio = 0.0f;        ///< グルーの調整比。>0 で伸ばす、<0 で縮める
    float naturalWidth = 0.0f; ///< 調整前の行長
    float width = 0.0f;        ///< 調整後の行長
};

class LineBreaker {
public:
    /**
     * 行分割を実行する
     * @param items 組版アイテム列（末尾に段落終端が付いていること）
     * @param lineLength 1 行の長さ（縦組みでは列の長さ）
     * @param opts オプション
     */
    static std::vector<BreakLine> breakLines(const std::vector<LineItem>& items,
                                             float lineLength,
                                             const LineBreakOptions& opts);
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_LINE_BREAKER_HPP
