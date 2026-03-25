/**
 * TagParser.cpp
 * 
 * HTMLライクなタグ付きテキストの解析
 */

#include "richtext/TagParser.hpp"
#include "richtext/FontManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stack>
#include <sstream>
#include <codecvt>
#include <locale>

namespace richtext {

//------------------------------------------------------------------------------
// 内部パース状態
//------------------------------------------------------------------------------

struct TagParser::ParseState {
    std::u16string plainText;       // タグ除去後のプレーンテキスト
    std::vector<TextSpan> spans;    // スタイル区間
    std::vector<std::string> errors; // エラーメッセージ
    
    // スタイルスタック（ネスト管理用）
    struct StyleContext {
        std::string tagName;
        TextStyle style;
        Appearance appearance;
        size_t startPos;            // プレーンテキスト内での開始位置
        
        // 特殊効果
        bool hasRuby = false;
        std::u16string rubyText;
        bool hasUnderline = false;
        bool hasStrikethrough = false;
        bool isSuperscript = false;
        bool isSubscript = false;
        float yOffset = 0.0f;
    };
    std::stack<StyleContext> styleStack;
    
    // 現在のスタイル
    TextStyle currentStyle;
    Appearance currentAppearance;
};

//------------------------------------------------------------------------------
// ユーティリティ
//------------------------------------------------------------------------------

namespace {

// UTF-8 → UTF-16 変換
std::u16string utf8ToUtf16(const std::string& utf8) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.from_bytes(utf8);
}

// UTF-16 → UTF-8 変換
std::string utf16ToUtf8(const std::u16string& utf16) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.to_bytes(utf16);
}

// 文字列を小文字に変換
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// 空白文字をスキップ
void skipWhitespace(const std::u16string& text, size_t& pos) {
    while (pos < text.size() && (text[pos] == u' ' || text[pos] == u'\t' ||
                                  text[pos] == u'\n' || text[pos] == u'\r')) {
        ++pos;
    }
}

// 識別子を取得
std::string parseIdentifier(const std::u16string& text, size_t& pos) {
    std::string result;
    while (pos < text.size()) {
        char16_t c = text[pos];
        if ((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
            (c >= u'0' && c <= u'9') || c == u'_' || c == u'-') {
            result += static_cast<char>(c);
            ++pos;
        } else {
            break;
        }
    }
    return result;
}

// 数値を解析
float parseFloat(const std::string& value) {
    try {
        return std::stof(value);
    } catch (...) {
        return 0.0f;
    }
}

int parseInt(const std::string& value) {
    try {
        if (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X") {
            return static_cast<int>(std::stoul(value, nullptr, 16));
        }
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

} // anonymous namespace

//------------------------------------------------------------------------------
// TagParser 実装
//------------------------------------------------------------------------------

TagParser::TagParser() = default;
TagParser::~TagParser() = default;

void TagParser::setOptions(const ParseOptions& options) {
    options_ = options;
}

//------------------------------------------------------------------------------
// メインパース関数
//------------------------------------------------------------------------------

TagParser::ParseResult TagParser::parse(
    const std::u16string& taggedText,
    const TextStyle& defaultStyle,
    const Appearance& defaultAppearance,
    const std::map<std::string, TextStyle>& namedStyles,
    const std::map<std::string, Appearance>& namedAppearances) {
    
    ParseState state;
    state.currentStyle = defaultStyle;
    state.currentAppearance = defaultAppearance;
    
    size_t pos = 0;
    size_t lastTextStart = 0;
    
    while (pos < taggedText.size()) {
        // タグの開始を探す
        if (taggedText[pos] == u'<') {
            // タグ前のテキストを処理
            size_t textEnd = pos;
            
            // 次の文字を確認
            if (pos + 1 < taggedText.size()) {
                char16_t nextChar = taggedText[pos + 1];
                
                if (nextChar == u'/') {
                    // 閉じタグ
                    if (!parseCloseTag(taggedText, pos, state)) {
                        // パース失敗、通常の文字として扱う
                        state.plainText += taggedText[pos];
                        ++pos;
                    }
                } else if ((nextChar >= u'a' && nextChar <= u'z') ||
                           (nextChar >= u'A' && nextChar <= u'Z')) {
                    // 開きタグ
                    if (!parseTag(taggedText, pos, state, defaultStyle, defaultAppearance,
                                  namedStyles, namedAppearances)) {
                        // パース失敗、通常の文字として扱う
                        state.plainText += taggedText[pos];
                        ++pos;
                    }
                } else {
                    // タグではない（例: < 5）
                    state.plainText += taggedText[pos];
                    ++pos;
                }
            } else {
                // ファイル末尾の '<'
                state.plainText += taggedText[pos];
                ++pos;
            }
        } else if (taggedText[pos] == u'&') {
            // エスケープシーケンス
            size_t start = pos;
            ++pos;
            std::string entityName;
            while (pos < taggedText.size() && taggedText[pos] != u';' &&
                   taggedText[pos] != u' ' && taggedText[pos] != u'<') {
                entityName += static_cast<char>(taggedText[pos]);
                ++pos;
            }
            
            if (pos < taggedText.size() && taggedText[pos] == u';') {
                ++pos;
                // エンティティを展開
                if (entityName == "lt") {
                    state.plainText += u'<';
                } else if (entityName == "gt") {
                    state.plainText += u'>';
                } else if (entityName == "amp") {
                    state.plainText += u'&';
                } else if (entityName == "quot") {
                    state.plainText += u'"';
                } else if (entityName == "apos") {
                    state.plainText += u'\'';
                } else if (entityName == "nbsp") {
                    state.plainText += u'\u00A0';  // ノーブレークスペース
                } else if (entityName.size() > 1 && entityName[0] == '#') {
                    // 数値参照
                    int codePoint = 0;
                    if (entityName[1] == 'x' || entityName[1] == 'X') {
                        codePoint = static_cast<int>(std::stoul(entityName.substr(2), nullptr, 16));
                    } else {
                        codePoint = std::stoi(entityName.substr(1));
                    }
                    if (codePoint > 0 && codePoint <= 0xFFFF) {
                        state.plainText += static_cast<char16_t>(codePoint);
                    }
                } else {
                    // 未知のエンティティ
                    state.plainText += u'&';
                    state.plainText += utf8ToUtf16(entityName);
                    state.plainText += u';';
                }
            } else {
                // 不完全なエンティティ
                for (size_t i = start; i < pos; ++i) {
                    state.plainText += taggedText[i];
                }
            }
        } else {
            // 通常の文字
            state.plainText += taggedText[pos];
            ++pos;
        }
    }
    
    // 未閉じタグの処理
    while (!state.styleStack.empty()) {
        auto& ctx = state.styleStack.top();
        
        // スパンを追加
        TextSpan span;
        span.start = ctx.startPos;
        span.end = state.plainText.size();
        span.style = ctx.style;
        span.appearance = ctx.appearance;
        span.hasRuby = ctx.hasRuby;
        span.rubyText = ctx.rubyText;
        span.hasUnderline = ctx.hasUnderline;
        span.hasStrikethrough = ctx.hasStrikethrough;
        span.isSuperscript = ctx.isSuperscript;
        span.isSubscript = ctx.isSubscript;
        span.yOffset = ctx.yOffset;
        
        if (span.end > span.start) {
            state.spans.push_back(span);
        }
        
        if (options_.strictMode) {
            state.errors.push_back("Unclosed tag: <" + ctx.tagName + ">");
        }
        
        state.styleStack.pop();
    }
    
    // ギャップ補填：タグで覆われていない領域にデフォルトスパンを挿入
    if (!state.plainText.empty()) {
        // 既存スパンを start 順にソート
        std::sort(state.spans.begin(), state.spans.end(),
                  [](const TextSpan& a, const TextSpan& b) { return a.start < b.start; });

        std::vector<TextSpan> filled;
        size_t cursor = 0;
        for (const auto& sp : state.spans) {
            if (sp.start > cursor) {
                // cursor..sp.start の隙間をデフォルトスパンで埋める
                TextSpan gap;
                gap.start = cursor;
                gap.end = sp.start;
                gap.style = defaultStyle;
                gap.appearance = defaultAppearance;
                filled.push_back(gap);
            }
            filled.push_back(sp);
            if (sp.end > cursor) cursor = sp.end;
        }
        // 末尾の隙間
        if (cursor < state.plainText.size()) {
            TextSpan gap;
            gap.start = cursor;
            gap.end = state.plainText.size();
            gap.style = defaultStyle;
            gap.appearance = defaultAppearance;
            filled.push_back(gap);
        }
        state.spans = std::move(filled);
    }
    
    // 結果を構築
    ParseResult result;
    result.plainText = std::move(state.plainText);
    result.spans = std::move(state.spans);
    result.errors = std::move(state.errors);
    result.hasErrors = !result.errors.empty();
    
    // StyleRun を構築
    result.styleRuns = buildStyleRuns(result);
    
    return result;
}

TagParser::ParseResult TagParser::parse(
    const std::string& taggedTextUtf8,
    const TextStyle& defaultStyle,
    const Appearance& defaultAppearance,
    const std::map<std::string, TextStyle>& namedStyles,
    const std::map<std::string, Appearance>& namedAppearances) {
    
    return parse(utf8ToUtf16(taggedTextUtf8), defaultStyle, defaultAppearance,
                 namedStyles, namedAppearances);
}

//------------------------------------------------------------------------------
// タグ解析
//------------------------------------------------------------------------------

bool TagParser::parseTag(
    const std::u16string& text, size_t& pos, ParseState& state,
    const TextStyle& defaultStyle,
    const Appearance& defaultAppearance,
    const std::map<std::string, TextStyle>& namedStyles,
    const std::map<std::string, Appearance>& namedAppearances) {
    
    size_t startPos = pos;
    
    // '<' をスキップ
    ++pos;
    
    // タグ名を取得
    std::string tagName = toLower(parseIdentifier(text, pos));
    if (tagName.empty()) {
        pos = startPos;
        return false;
    }
    
    // 属性を解析
    skipWhitespace(text, pos);
    auto attrs = parseAttributes(text, pos);
    
    // '>' または '/>' を探す
    skipWhitespace(text, pos);
    bool selfClosing = false;
    if (pos < text.size() && text[pos] == u'/') {
        selfClosing = true;
        ++pos;
    }
    
    if (pos >= text.size() || text[pos] != u'>') {
        pos = startPos;
        return false;
    }
    ++pos;  // '>' をスキップ
    
    // タグを処理
    TextStyle newStyle = state.currentStyle;
    Appearance newAppearance = state.currentAppearance;
    bool hasRuby = false;
    std::u16string rubyText;
    bool hasUnderline = false;
    bool hasStrikethrough = false;
    bool isSuperscript = false;
    bool isSubscript = false;
    float yOffset = 0.0f;
    bool isVoidTag = false;  // 閉じタグ不要のタグ
    
    if (tagName == "font") {
        newStyle = applyFontTag(state.currentStyle, attrs);
    } else if (tagName == "b" || tagName == "strong") {
        newStyle = applyBoldTag(state.currentStyle);
    } else if (tagName == "i" || tagName == "em") {
        newStyle = applyItalicTag(state.currentStyle);
    } else if (tagName == "u") {
        hasUnderline = true;
    } else if (tagName == "s" || tagName == "strike" || tagName == "del") {
        hasStrikethrough = true;
    } else if (tagName == "sup") {
        newStyle = applySupTag(state.currentStyle);
        isSuperscript = true;
        // Y オフセットは現在のフォントサイズ基準（em単位 → ピクセル変換）
        yOffset = options_.supOffset * state.currentStyle.fontSize;
    } else if (tagName == "sub") {
        newStyle = applySubTag(state.currentStyle);
        isSubscript = true;
        yOffset = options_.subOffset * state.currentStyle.fontSize;
    } else if (tagName == "color") {
        newAppearance = applyColorTag(state.currentAppearance, attrs);
    } else if (tagName == "outline") {
        newAppearance = applyOutlineTag(state.currentAppearance, attrs);
    } else if (tagName == "shadow") {
        newAppearance = applyShadowTag(state.currentAppearance, attrs);
    } else if (tagName == "ruby") {
        hasRuby = true;
        auto it = attrs.find("text");
        if (it != attrs.end()) {
            rubyText = utf8ToUtf16(it->second);
        }
    } else if (tagName == "style") {
        auto it = attrs.find("name");
        if (it != attrs.end()) {
            std::string styleName = it->second;
            auto styleIt = namedStyles.find(styleName);
            if (styleIt != namedStyles.end()) {
                newStyle = styleIt->second;
            }
            auto appIt = namedAppearances.find(styleName);
            if (appIt != namedAppearances.end()) {
                newAppearance = appIt->second;
            }
        }
    } else if (tagName == "br") {
        // 改行
        state.plainText += u'\n';
        isVoidTag = true;
    } else if (tagName == "sp") {
        // スペース
        auto it = attrs.find("width");
        int width = 1;
        if (it != attrs.end()) {
            width = parseInt(it->second);
            if (width < 1) width = 1;
        }
        for (int i = 0; i < width; ++i) {
            state.plainText += u' ';
        }
        isVoidTag = true;
    } else {
        // 未知のタグ
        if (!options_.ignoreUnknownTags) {
            state.errors.push_back("Unknown tag: <" + tagName + ">");
        }
        // 無視して処理を続行
        return true;
    }
    
    // 自己閉じタグまたはvoidタグの場合はスタックに積まない
    if (selfClosing || isVoidTag) {
        return true;
    }
    
    // 現在のスタイルをスタックに積む
    ParseState::StyleContext ctx;
    ctx.tagName = tagName;
    ctx.style = newStyle;
    ctx.appearance = newAppearance;
    ctx.startPos = state.plainText.size();
    ctx.hasRuby = hasRuby;
    ctx.rubyText = rubyText;
    ctx.hasUnderline = hasUnderline;
    ctx.hasStrikethrough = hasStrikethrough;
    ctx.isSuperscript = isSuperscript;
    ctx.isSubscript = isSubscript;
    ctx.yOffset = yOffset;

    state.styleStack.push(ctx);
    state.currentStyle = newStyle;
    state.currentAppearance = newAppearance;
    
    return true;
}

bool TagParser::parseCloseTag(const std::u16string& text, size_t& pos, ParseState& state) {
    size_t startPos = pos;
    
    // '</' をスキップ
    pos += 2;
    
    // タグ名を取得
    std::string tagName = toLower(parseIdentifier(text, pos));
    if (tagName.empty()) {
        pos = startPos;
        return false;
    }
    
    // '>' を探す
    skipWhitespace(text, pos);
    if (pos >= text.size() || text[pos] != u'>') {
        pos = startPos;
        return false;
    }
    ++pos;  // '>' をスキップ
    
    // スタックから対応する開きタグを探す
    if (state.styleStack.empty()) {
        state.errors.push_back("Unexpected closing tag: </" + tagName + ">");
        return true;  // エラーだがパースは成功
    }
    
    auto& top = state.styleStack.top();
    
    if (top.tagName != tagName) {
        // タグの不一致
        if (options_.strictMode) {
            state.errors.push_back("Mismatched closing tag: expected </" + 
                                  top.tagName + ">, got </" + tagName + ">");
        }
        // それでも閉じる（寛容モード）
    }
    
    // スパンを追加
    TextSpan span;
    span.start = top.startPos;
    span.end = state.plainText.size();
    span.style = top.style;
    span.appearance = top.appearance;
    span.hasRuby = top.hasRuby;
    span.rubyText = top.rubyText;
    span.hasUnderline = top.hasUnderline;
    span.hasStrikethrough = top.hasStrikethrough;
    span.isSuperscript = top.isSuperscript;
    span.isSubscript = top.isSubscript;
    span.yOffset = top.yOffset;

    if (span.end > span.start) {
        state.spans.push_back(span);
    }

    state.styleStack.pop();
    
    // 親のスタイルに戻す
    if (!state.styleStack.empty()) {
        state.currentStyle = state.styleStack.top().style;
        state.currentAppearance = state.styleStack.top().appearance;
    }
    
    return true;
}

//------------------------------------------------------------------------------
// 属性解析
//------------------------------------------------------------------------------

std::map<std::string, std::string> TagParser::parseAttributes(
    const std::u16string& text, size_t& pos) {
    
    std::map<std::string, std::string> attrs;
    
    while (pos < text.size()) {
        skipWhitespace(text, pos);
        
        // '>' または '/' で終了
        if (pos >= text.size() || text[pos] == u'>' || text[pos] == u'/') {
            break;
        }
        
        // 属性名を取得
        std::string attrName = toLower(parseIdentifier(text, pos));
        if (attrName.empty()) {
            break;
        }
        
        skipWhitespace(text, pos);
        
        // '=' があれば値を取得
        std::string attrValue;
        if (pos < text.size() && text[pos] == u'=') {
            ++pos;
            skipWhitespace(text, pos);
            attrValue = parseAttributeValue(text, pos);
        }
        
        attrs[attrName] = attrValue;
    }
    
    return attrs;
}

std::string TagParser::parseAttributeValue(const std::u16string& text, size_t& pos) {
    if (pos >= text.size()) {
        return "";
    }
    
    std::string result;
    
    char16_t quote = 0;
    if (text[pos] == u'"' || text[pos] == u'\'') {
        quote = text[pos];
        ++pos;
    }
    
    while (pos < text.size()) {
        char16_t c = text[pos];
        
        if (quote != 0) {
            if (c == quote) {
                ++pos;
                break;
            }
        } else {
            if (c == u' ' || c == u'\t' || c == u'>' || c == u'/') {
                break;
            }
        }
        
        result += static_cast<char>(c);
        ++pos;
    }
    
    return result;
}

//------------------------------------------------------------------------------
// 色値パース
//------------------------------------------------------------------------------

uint32_t TagParser::parseColorValue(const std::string& value) {
    std::string v = value;
    
    // 先頭の # を除去
    if (!v.empty() && v[0] == '#') {
        v = v.substr(1);
    }
    
    // 0x プレフィックスを除去
    if (v.size() >= 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        v = v.substr(2);
    }
    
    try {
        if (v.size() == 6) {
            // RRGGBB → 0xFFRRGGBB
            return 0xFF000000 | static_cast<uint32_t>(std::stoul(v, nullptr, 16));
        } else if (v.size() == 8) {
            // AARRGGBB
            return static_cast<uint32_t>(std::stoul(v, nullptr, 16));
        }
    } catch (...) {
    }
    
    return 0xFFFFFFFF;  // デフォルト: 白
}

//------------------------------------------------------------------------------
// スタイル適用
//------------------------------------------------------------------------------

TextStyle TagParser::applyFontTag(const TextStyle& current,
                                  const std::map<std::string, std::string>& attrs) {
    TextStyle style = current;
    
    auto it = attrs.find("size");
    if (it != attrs.end()) {
        style.fontSize = parseFloat(it->second);
    }
    
    it = attrs.find("weight");
    if (it != attrs.end()) {
        style.fontWeight = static_cast<uint16_t>(parseInt(it->second));
    }
    
    it = attrs.find("spacing");
    if (it != attrs.end()) {
        style.letterSpacing = parseFloat(it->second);
    }

    it = attrs.find("face");
    if (it != attrs.end() && !it->second.empty()) {
        auto collection = FontManager::instance().createCollection({it->second});
        if (collection) {
            style.fontCollection = collection;
        }
    }

    return style;
}

TextStyle TagParser::applyBoldTag(const TextStyle& current) {
    TextStyle style = current;
    style.fontWeight = 700;
    return style;
}

TextStyle TagParser::applyItalicTag(const TextStyle& current) {
    TextStyle style = current;
    style.italic = true;
    return style;
}

TextStyle TagParser::applySupTag(const TextStyle& current) {
    TextStyle style = current;
    style.fontSize = current.fontSize * options_.supScale;
    return style;
}

TextStyle TagParser::applySubTag(const TextStyle& current) {
    TextStyle style = current;
    style.fontSize = current.fontSize * options_.subScale;
    return style;
}

Appearance TagParser::applyColorTag(const Appearance& current,
                                    const std::map<std::string, std::string>& attrs) {
    Appearance app;  // 新しい外観
    
    uint32_t color = 0xFFFFFFFF;
    
    auto it = attrs.find("value");
    if (it != attrs.end()) {
        color = parseColorValue(it->second);
    } else {
        // 個別の R, G, B, A
        uint8_t r = 255, g = 255, b = 255, a = 255;
        it = attrs.find("r");
        if (it != attrs.end()) r = static_cast<uint8_t>(parseInt(it->second));
        it = attrs.find("g");
        if (it != attrs.end()) g = static_cast<uint8_t>(parseInt(it->second));
        it = attrs.find("b");
        if (it != attrs.end()) b = static_cast<uint8_t>(parseInt(it->second));
        it = attrs.find("a");
        if (it != attrs.end()) a = static_cast<uint8_t>(parseInt(it->second));
        
        color = (static_cast<uint32_t>(a) << 24) |
                (static_cast<uint32_t>(r) << 16) |
                (static_cast<uint32_t>(g) << 8) |
                static_cast<uint32_t>(b);
    }
    
    app.addFill(color);
    return app;
}

Appearance TagParser::applyOutlineTag(const Appearance& current,
                                      const std::map<std::string, std::string>& attrs) {
    Appearance app = current;
    
    uint32_t color = 0xFF000000;  // デフォルト: 黒
    float width = 2.0f;
    float ox = 0, oy = 0;
    
    auto it = attrs.find("color");
    if (it != attrs.end()) {
        color = parseColorValue(it->second);
    }
    
    it = attrs.find("width");
    if (it != attrs.end()) {
        width = parseFloat(it->second);
    }
    
    it = attrs.find("x");
    if (it != attrs.end()) ox = parseFloat(it->second);
    
    it = attrs.find("y");
    if (it != attrs.end()) oy = parseFloat(it->second);
    
    // 縁取りをストロークとして追加（塗りより先に描画されるよう先頭に挿入）
    DrawStyle stroke;
    stroke.type = DrawStyle::Type::Stroke;
    stroke.setColor(color);
    stroke.strokeWidth = width;
    stroke.offsetX = ox;
    stroke.offsetY = oy;
    stroke.strokeJoin = tvg::StrokeJoin::Round;
    
    // 既存のスタイルを保持しつつストロークを追加
    app.addStroke(stroke);
    
    return app;
}

Appearance TagParser::applyShadowTag(const Appearance& current,
                                     const std::map<std::string, std::string>& attrs) {
    Appearance app = current;
    
    uint32_t color = 0x80000000;  // デフォルト: 半透明黒
    float ox = 2.0f, oy = 2.0f;
    
    auto it = attrs.find("color");
    if (it != attrs.end()) {
        color = parseColorValue(it->second);
    }
    
    it = attrs.find("x");
    if (it != attrs.end()) ox = parseFloat(it->second);
    
    it = attrs.find("y");
    if (it != attrs.end()) oy = parseFloat(it->second);
    
    // 影を塗りとして追加（先に描画されるよう）
    app.addFill(color, ox, oy);
    
    return app;
}

//------------------------------------------------------------------------------
// StyleRun 構築
//------------------------------------------------------------------------------

std::vector<ParagraphLayout::StyleRun> TagParser::buildStyleRuns(
    const ParseResult& result) {
    
    std::vector<ParagraphLayout::StyleRun> runs;
    
    if (result.spans.empty()) {
        return runs;
    }
    
    // スパンを開始位置でソート
    auto sortedSpans = result.spans;
    std::sort(sortedSpans.begin(), sortedSpans.end(),
              [](const TextSpan& a, const TextSpan& b) {
                  return a.start < b.start;
              });
    
    // 重複するスパンをマージ/分割してStyleRunに変換
    for (const auto& span : sortedSpans) {
        ParagraphLayout::StyleRun run;
        run.start = span.start;
        run.end = span.end;
        run.style = span.style;
        runs.push_back(run);
    }
    
    return runs;
}

//------------------------------------------------------------------------------
// 静的ユーティリティ
//------------------------------------------------------------------------------

std::u16string TagParser::stripTags(const std::u16string& taggedText) {
    std::u16string result;
    size_t pos = 0;
    
    while (pos < taggedText.size()) {
        if (taggedText[pos] == u'<') {
            // タグをスキップ
            while (pos < taggedText.size() && taggedText[pos] != u'>') {
                ++pos;
            }
            if (pos < taggedText.size()) {
                ++pos;  // '>' をスキップ
            }
        } else {
            result += taggedText[pos];
            ++pos;
        }
    }
    
    return result;
}

std::u16string TagParser::unescapeText(const std::u16string& text) {
    std::u16string result;
    size_t pos = 0;
    
    while (pos < text.size()) {
        if (text[pos] == u'&') {
            size_t start = pos;
            ++pos;
            std::string entity;
            while (pos < text.size() && text[pos] != u';' && text[pos] != u' ') {
                entity += static_cast<char>(text[pos]);
                ++pos;
            }
            if (pos < text.size() && text[pos] == u';') {
                ++pos;
                if (entity == "lt") result += u'<';
                else if (entity == "gt") result += u'>';
                else if (entity == "amp") result += u'&';
                else if (entity == "quot") result += u'"';
                else if (entity == "apos") result += u'\'';
                else if (entity == "nbsp") result += u'\u00A0';
                else {
                    // 未知のエンティティはそのまま
                    for (size_t i = start; i < pos; ++i) {
                        result += text[i];
                    }
                }
            } else {
                for (size_t i = start; i < pos; ++i) {
                    result += text[i];
                }
            }
        } else {
            result += text[pos];
            ++pos;
        }
    }
    
    return result;
}

std::u16string TagParser::escapeText(const std::u16string& text) {
    std::u16string result;
    
    for (char16_t c : text) {
        switch (c) {
        case u'<': result += u"&lt;"; break;
        case u'>': result += u"&gt;"; break;
        case u'&': result += u"&amp;"; break;
        case u'"': result += u"&quot;"; break;
        case u'\'': result += u"&apos;"; break;
        default: result += c; break;
        }
    }
    
    return result;
}

} // namespace richtext
