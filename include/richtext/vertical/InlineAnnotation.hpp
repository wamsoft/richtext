#ifndef RICHTEXT_VERTICAL_INLINE_ANNOTATION_HPP
#define RICHTEXT_VERTICAL_INLINE_ANNOTATION_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * InlineAnnotation — 行内に組み込む注記
 *
 * ルビ・縦中横・圏点・割注・字取りを、本文の文字範囲に対する注記として表す。
 * 組版層（LineItemBuilder）がこれを解釈して Box / Glue / Penalty 列に
 * 落とし込むので、行分割・約物の詰め・両端揃えと矛盾しない。
 *
 * 範囲は本文の UTF-16 単位で [start, end)。範囲が重なる注記は、種類が違えば
 * 併用できる（ルビと圏点など）が、同じ種類の重なりは未定義。
 */
namespace richtext::vertical {

enum class AnnotationType : uint8_t {
    Ruby,           ///< ルビ（振り仮名）
    TateChuYoko,    ///< 縦中横（半角数字等を 1em 角に正立で収める）
    Emphasis,       ///< 圏点（親文字の右に付ける）
    Warichu,        ///< 割注（行内に 2 行の子ブロックを組む）
    Jidori,         ///< 字取り（指定 em 数へ均等割り付け）
};

/**
 * ルビの掛け方
 */
enum class RubyMode : uint8_t {
    /// グループルビ。親文字列全体に 1 つのルビを中付きで配置する
    Group,
    /// モノルビ。`text` を `|` で区切って親文字 1 文字ずつに対応させる。
    /// 区切りの数が親文字数と合わない場合は Group として扱う
    Mono,
};

/**
 * 圏点の種類
 */
enum class EmphasisMark : uint8_t {
    Sesame,         ///< ゴマ点 U+FE45
    OpenSesame,     ///< 白ゴマ点 U+FE46
    Dot,            ///< 中点 U+30FB
    FilledCircle,   ///< ● U+25CF
    OpenCircle,     ///< ○ U+25CB
};

/// 圏点の種類 → コードポイント
char32_t emphasisMarkCodePoint(EmphasisMark mark);

/**
 * 注記 1 個
 */
struct InlineAnnotation {
    AnnotationType type = AnnotationType::Ruby;

    /// 本文中の範囲（UTF-16 単位、[start, end)）
    size_t start = 0;
    size_t end = 0;

    /// ルビ文字列 / 割注の内容
    std::u16string text;

    /// ルビの掛け方
    RubyMode rubyMode = RubyMode::Group;

    /// ルビ・割注・圏点の文字サイズ倍率（親文字に対する比）
    float scale = 0.5f;

    /// 圏点の種類
    EmphasisMark mark = EmphasisMark::Sesame;

    /// 字取りの長さ（em 単位）
    float jidoriEm = 0.0f;

    // --- 生成ヘルパー ---

    static InlineAnnotation ruby(size_t start, size_t end, std::u16string text,
                                 RubyMode mode = RubyMode::Group, float scale = 0.5f) {
        InlineAnnotation a;
        a.type = AnnotationType::Ruby;
        a.start = start;
        a.end = end;
        a.text = std::move(text);
        a.rubyMode = mode;
        a.scale = scale;
        return a;
    }

    static InlineAnnotation tateChuYoko(size_t start, size_t end) {
        InlineAnnotation a;
        a.type = AnnotationType::TateChuYoko;
        a.start = start;
        a.end = end;
        return a;
    }

    static InlineAnnotation emphasis(size_t start, size_t end,
                                     EmphasisMark mark = EmphasisMark::Sesame,
                                     float scale = 0.5f) {
        InlineAnnotation a;
        a.type = AnnotationType::Emphasis;
        a.start = start;
        a.end = end;
        a.mark = mark;
        a.scale = scale;
        return a;
    }

    static InlineAnnotation warichu(size_t start, size_t end, std::u16string text,
                                    float scale = 0.5f) {
        InlineAnnotation a;
        a.type = AnnotationType::Warichu;
        a.start = start;
        a.end = end;
        a.text = std::move(text);
        a.scale = scale;
        return a;
    }

    static InlineAnnotation jidori(size_t start, size_t end, float em) {
        InlineAnnotation a;
        a.type = AnnotationType::Jidori;
        a.start = start;
        a.end = end;
        a.jidoriEm = em;
        return a;
    }
};

} // namespace richtext::vertical

#endif // RICHTEXT_VERTICAL_INLINE_ANNOTATION_HPP
