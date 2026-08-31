#ifndef RICHTEXT_VERTICAL_BLOCK_LAYOUT_HPP
#define RICHTEXT_VERTICAL_BLOCK_LAYOUT_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "richtext/TextStyle.hpp"
#include "richtext/vertical/InlineAnnotation.hpp"
#include "richtext/vertical/VerticalParagraphLayout.hpp"

/**
 * BlockLayout — 段組・連結コンテナへの流し込み・ページ分割
 *
 * 連結された複数のコンテナ（＝ページや枠）へ段落列を順に流し込む。
 * 各コンテナは段（column）に分かれ、縦組みでは段が上下に並び、段の中では
 * 行（列）が右から左（vertical-rl）へ進む。
 *
 * ```
 *   コンテナ（1 ページ）
 *   ┌───────────────────────────┐
 *   │ 段 0   ←── 行が右から左へ ←── │
 *   ├───────────────────────────┤  ← 段間
 *   │ 段 1   ←────────────────── │
 *   └───────────────────────────┘
 * ```
 *
 * 行長は段の高さで決まる。段の高さが変わるコンテナへまたがる場合は、
 * 残りのテキストをその行長で組み直す（TLF の ContainerController と同じ考え方）。
 */
namespace richtext::vertical {

/**
 * 流し込み先のコンテナ（版面）
 */
struct BlockContainer {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    /// 段数（縦組みでは上下に並ぶ）
    int columnCount = 1;
    /// 段間（ピクセル）
    float columnGap = 0.0f;

    /// 段 s の矩形（y と高さ）
    float sectionHeight() const {
        if (columnCount <= 1) return height;
        const float total = height - columnGap * static_cast<float>(columnCount - 1);
        return total / static_cast<float>(columnCount);
    }
    float sectionY(int s) const {
        return y + static_cast<float>(s) * (sectionHeight() + columnGap);
    }
};

/**
 * 配置済みの 1 行
 */
struct PlacedLine {
    uint32_t chunkIndex = 0;      ///< BlockLayout::getChunk() のインデックス
    uint32_t lineIndex = 0;       ///< チャンク内の行インデックス
    uint32_t containerIndex = 0;
    uint32_t sectionIndex = 0;    ///< コンテナ内の段番号
    float x = 0.0f;               ///< 縦ベースライン（列の中心線）のX
    float y = 0.0f;               ///< 行頭のY
};

class BlockLayout {
public:
    /**
     * 流し込む段落
     */
    struct Paragraph {
        std::u16string text;
        std::vector<InlineAnnotation> annotations;
        TextStyle style;
        VerticalLayoutOptions options;
        /// 段落の後に空ける量（列送り方向、ピクセル）
        float spaceAfter = 0.0f;
    };

    /**
     * 組み終わったテキストの断片
     *
     * 段の高さが変わるところで組み直すので、1 段落が複数の断片に割れることがある。
     */
    struct Chunk {
        size_t paragraphIndex = 0;
        size_t charOffset = 0;    ///< 段落テキスト内での開始位置（UTF-16）
        VerticalParagraphLayout layout;
    };

    /// 入りきらなかった残り
    struct Overflow {
        bool has = false;
        size_t paragraphIndex = 0;
        size_t charOffset = 0;
    };

    BlockLayout() = default;

    /**
     * 流し込み先を設定する（順に埋めていく）
     */
    void setContainers(std::vector<BlockContainer> containers) {
        containers_ = std::move(containers);
    }
    const std::vector<BlockContainer>& getContainers() const { return containers_; }

    /**
     * 流し込みを実行する
     */
    void layout(const std::vector<Paragraph>& paragraphs);

    // ------------------------------------------------------------------
    // 結果
    // ------------------------------------------------------------------

    size_t getPlacedLineCount() const { return placed_.size(); }
    const PlacedLine& getPlacedLine(size_t index) const { return placed_[index]; }
    const std::vector<PlacedLine>& getPlacedLines() const { return placed_; }

    const Chunk& getChunk(size_t index) const { return chunks_[index]; }
    size_t getChunkCount() const { return chunks_.size(); }

    /// 配置済み行が指す行データ
    const VerticalParagraphLayout::Line& getLine(const PlacedLine& pl) const {
        return chunks_[pl.chunkIndex].layout.getLine(pl.lineIndex);
    }
    /// 配置済み行のスタイル
    const TextStyle& getStyle(const PlacedLine& pl) const {
        return chunks_[pl.chunkIndex].layout.getStyle();
    }

    /**
     * コンテナ index に配置された行の範囲（[first, first + count)）
     * 行は流し込み順に並んでいるので連続している。
     */
    void getContainerLineRange(size_t index, size_t& first, size_t& count) const;

    /// 使ったコンテナ数
    size_t getUsedContainerCount() const { return usedContainers_; }

    const Overflow& getOverflow() const { return overflow_; }

private:
    std::vector<BlockContainer> containers_;
    std::vector<Chunk> chunks_;
    std::vector<PlacedLine> placed_;
    Overflow overflow_;
    size_t usedContainers_ = 0;
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_BLOCK_LAYOUT_HPP
