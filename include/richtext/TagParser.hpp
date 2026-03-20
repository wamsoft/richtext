#ifndef RICHTEXT_TAG_PARSER_HPP
#define RICHTEXT_TAG_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/ParagraphLayout.hpp"

namespace richtext {

/**
 * タグパーサー
 * 
 * HTMLライクなタグ付きテキストを解析し、スタイル区間に分解する。
 * 
 * 対応タグ:
 * - <font face='...' size=... weight=...> フォント変更
 * - <color value=0xAARRGGBB> または <color r=... g=... b=... a=...> 色変更
 * - <b> 太字
 * - <i> 斜体
 * - <u> 下線
 * - <s> 取り消し線
 * - <ruby text='...'> ルビ
 * - <sup> 上付き文字
 * - <sub> 下付き文字
 * - <style name='...'> 名前付きスタイル参照
 * - <br> 改行
 * - <sp width=...> スペース
 * - <outline color=... width=...> 縁取り
 * - <shadow color=... x=... y=...> 影
 */
class TagParser {
public:
    /**
     * テキストスパン（スタイル適用区間）
     */
    struct TextSpan {
        size_t start;               ///< 開始位置（プレーンテキスト内）
        size_t end;                 ///< 終了位置
        TextStyle style;            ///< テキストスタイル
        Appearance appearance;      ///< 描画外観
        
        // 特殊効果
        bool hasRuby = false;           ///< ルビあり
        std::u16string rubyText;        ///< ルビテキスト
        bool hasUnderline = false;      ///< 下線あり
        bool hasStrikethrough = false;  ///< 取り消し線あり
        
        // 上付き/下付き
        bool isSuperscript = false;     ///< 上付き文字
        bool isSubscript = false;       ///< 下付き文字
    };
    
    /**
     * パース結果
     */
    struct ParseResult {
        std::u16string plainText;                       ///< タグ除去後のプレーンテキスト
        std::vector<TextSpan> spans;                    ///< スタイル適用区間
        std::vector<ParagraphLayout::StyleRun> styleRuns;  ///< minikin用StyleRun
        
        /// パースエラーがあったか
        bool hasErrors = false;
        /// エラーメッセージ
        std::vector<std::string> errors;
    };
    
    /**
     * パースオプション
     */
    struct ParseOptions {
        bool allowNestedTags = true;        ///< ネストタグを許可
        bool ignoreUnknownTags = true;      ///< 未知のタグを無視（false: エラー）
        bool strictMode = false;            ///< 厳格モード（閉じタグ必須等）
        
        float rubyScale = 0.5f;             ///< ルビのサイズ倍率
        float supScale = 0.7f;              ///< 上付き文字の倍率
        float subScale = 0.7f;              ///< 下付き文字の倍率
        float supOffset = -0.4f;            ///< 上付き文字のY オフセット（em単位）
        float subOffset = 0.2f;             ///< 下付き文字のY オフセット（em単位）
    };
    
    /**
     * コンストラクタ
     */
    TagParser();
    
    /**
     * デストラクタ
     */
    ~TagParser();
    
    /**
     * パースオプションの設定
     */
    void setOptions(const ParseOptions& options);
    
    /**
     * パースオプションの取得
     */
    const ParseOptions& getOptions() const { return options_; }
    
    /**
     * タグ付きテキストの解析
     * 
     * @param taggedText タグ付きテキスト（UTF-16）
     * @param defaultStyle デフォルトのテキストスタイル
     * @param defaultAppearance デフォルトの描画外観
     * @param namedStyles 名前付きスタイルのマップ（<style name='...'>用）
     * @param namedAppearances 名前付き外観のマップ
     * @return パース結果
     */
    ParseResult parse(
        const std::u16string& taggedText,
        const TextStyle& defaultStyle,
        const Appearance& defaultAppearance,
        const std::map<std::string, TextStyle>& namedStyles = {},
        const std::map<std::string, Appearance>& namedAppearances = {}
    );
    
    /**
     * タグ付きテキストの解析（UTF-8版）
     */
    ParseResult parse(
        const std::string& taggedTextUtf8,
        const TextStyle& defaultStyle,
        const Appearance& defaultAppearance,
        const std::map<std::string, TextStyle>& namedStyles = {},
        const std::map<std::string, Appearance>& namedAppearances = {}
    );
    
    /**
     * スタイル継承を考慮したStyleRun配列の生成
     * 
     * @param result パース結果
     * @return ParagraphLayout用のStyleRun配列
     */
    static std::vector<ParagraphLayout::StyleRun> buildStyleRuns(
        const ParseResult& result
    );
    
    /**
     * プレーンテキストへの変換（タグを除去）
     * 
     * @param taggedText タグ付きテキスト
     * @return タグを除去したプレーンテキスト
     */
    static std::u16string stripTags(const std::u16string& taggedText);
    
    /**
     * エスケープ文字の展開
     * 
     * @param text エスケープ文字を含むテキスト
     * @return 展開後のテキスト
     */
    static std::u16string unescapeText(const std::u16string& text);
    
    /**
     * テキストのエスケープ
     * 
     * @param text プレーンテキスト
     * @return エスケープ済みテキスト
     */
    static std::u16string escapeText(const std::u16string& text);

private:
    ParseOptions options_;
    
    // 内部パース状態
    struct ParseState;
    
    // タグの解析
    bool parseTag(const std::u16string& text, size_t& pos, ParseState& state,
                  const TextStyle& defaultStyle,
                  const Appearance& defaultAppearance,
                  const std::map<std::string, TextStyle>& namedStyles,
                  const std::map<std::string, Appearance>& namedAppearances);
    
    // 閉じタグの解析
    bool parseCloseTag(const std::u16string& text, size_t& pos, ParseState& state);
    
    // 属性の解析
    std::map<std::string, std::string> parseAttributes(
        const std::u16string& text, size_t& pos);
    
    // 属性値の解析
    std::string parseAttributeValue(const std::u16string& text, size_t& pos);
    
    // 色値のパース（0xAARRGGBB, #RRGGBB 形式）
    static uint32_t parseColorValue(const std::string& value);
    
    // タグからスタイル変更を適用
    TextStyle applyFontTag(const TextStyle& current,
                           const std::map<std::string, std::string>& attrs);
    TextStyle applyBoldTag(const TextStyle& current);
    TextStyle applyItalicTag(const TextStyle& current);
    TextStyle applySupTag(const TextStyle& current);
    TextStyle applySubTag(const TextStyle& current);
    
    Appearance applyColorTag(const Appearance& current,
                             const std::map<std::string, std::string>& attrs);
    Appearance applyOutlineTag(const Appearance& current,
                               const std::map<std::string, std::string>& attrs);
    Appearance applyShadowTag(const Appearance& current,
                              const std::map<std::string, std::string>& attrs);
    
    // スパンの追加
    void pushSpan(ParseState& state, const std::string& tagName,
                  const TextStyle& style, const Appearance& appearance);
    void popSpan(ParseState& state, const std::string& tagName);
};

} // namespace richtext

#endif // RICHTEXT_TAG_PARSER_HPP
