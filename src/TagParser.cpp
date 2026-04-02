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

    // デフォルトスタイル（スタックが空になった際の復元用）
    TextStyle defaultStyle;
    Appearance defaultAppearance;

    // --- タイミング状態 ---
    float currentDelayPercent = 100.0f;
    float currentDelayMs = -1.0f;
    std::vector<TimingEntry> timings;

    // --- リンク状態 ---
    int currentLinkIndex = -1;
    std::vector<LinkInfo> links;

    // --- グラフィック ---
    std::vector<GraphInfo> graphics;
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
// プレーンテキスト文字追加ヘルパー
//------------------------------------------------------------------------------

void TagParser::addPlainChar(ParseState& state, char16_t ch) {
    int charIndex = static_cast<int>(state.plainText.size());
    state.plainText += ch;

    // タイミングエントリ生成
    if (!options_.ignoreDelay) {
        TimingEntry entry;
        entry.type = TimingEntry::Type::Char;
        entry.charIndex = charIndex;
        entry.delayPercent = state.currentDelayPercent;
        entry.delayMs = state.currentDelayMs;
        state.timings.push_back(entry);
    }

    // リンク endIndex 更新
    if (state.currentLinkIndex >= 0 &&
        state.currentLinkIndex < static_cast<int>(state.links.size())) {
        state.links[state.currentLinkIndex].endIndex = state.plainText.size();
    }
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
    state.defaultStyle = defaultStyle;
    state.defaultAppearance = defaultAppearance;

    size_t pos = 0;

    while (pos < taggedText.size()) {
        // タグの開始を探す
        if (taggedText[pos] == u'<') {
            // 次の文字を確認
            if (pos + 1 < taggedText.size()) {
                char16_t nextChar = taggedText[pos + 1];

                if (nextChar == u'/') {
                    // 閉じタグ
                    if (!parseCloseTag(taggedText, pos, state)) {
                        addPlainChar(state, taggedText[pos]);
                        ++pos;
                    }
                } else if ((nextChar >= u'a' && nextChar <= u'z') ||
                           (nextChar >= u'A' && nextChar <= u'Z')) {
                    // 開きタグ
                    if (!parseTag(taggedText, pos, state, defaultStyle, defaultAppearance,
                                  namedStyles, namedAppearances)) {
                        addPlainChar(state, taggedText[pos]);
                        ++pos;
                    }
                } else {
                    // タグではない（例: < 5）
                    addPlainChar(state, taggedText[pos]);
                    ++pos;
                }
            } else {
                // ファイル末尾の '<'
                addPlainChar(state, taggedText[pos]);
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
                    addPlainChar(state, u'<');
                } else if (entityName == "gt") {
                    addPlainChar(state, u'>');
                } else if (entityName == "amp") {
                    addPlainChar(state, u'&');
                } else if (entityName == "quot") {
                    addPlainChar(state, u'"');
                } else if (entityName == "apos") {
                    addPlainChar(state, u'\'');
                } else if (entityName == "nbsp") {
                    addPlainChar(state, u'\u00A0');
                } else if (entityName.size() > 1 && entityName[0] == '#') {
                    // 数値参照
                    int codePoint = 0;
                    if (entityName[1] == 'x' || entityName[1] == 'X') {
                        codePoint = static_cast<int>(std::stoul(entityName.substr(2), nullptr, 16));
                    } else {
                        codePoint = std::stoi(entityName.substr(1));
                    }
                    if (codePoint > 0 && codePoint <= 0xFFFF) {
                        addPlainChar(state, static_cast<char16_t>(codePoint));
                    }
                } else {
                    // 未知のエンティティ
                    addPlainChar(state, u'&');
                    std::u16string entityU16 = utf8ToUtf16(entityName);
                    for (char16_t c : entityU16) addPlainChar(state, c);
                    addPlainChar(state, u';');
                }
            } else {
                // 不完全なエンティティ
                for (size_t i = start; i < pos; ++i) {
                    addPlainChar(state, taggedText[i]);
                }
            }
        } else {
            // 通常の文字
            addPlainChar(state, taggedText[pos]);
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

        // link の未閉じ処理
        if (ctx.tagName == "link") {
            state.currentLinkIndex = -1;
        }

        state.styleStack.pop();
    }

    // ギャップ補填：ネストされたタグによる重複スパンを解決し、
    // 各文字位置に最も内側（最も具体的な）スタイルを適用する。
    if (!state.plainText.empty()) {
        size_t n = state.plainText.size();
        std::vector<int> charSpanIdx(n, -1);

        for (int si = static_cast<int>(state.spans.size()) - 1; si >= 0; si--) {
            const auto& sp = state.spans[si];
            size_t end = std::min(sp.end, n);
            for (size_t j = sp.start; j < end; j++) {
                charSpanIdx[j] = si;
            }
        }

        std::vector<TextSpan> filled;
        size_t runStart = 0;
        int prevIdx = charSpanIdx[0];

        for (size_t i = 1; i <= n; i++) {
            int curIdx = (i < n) ? charSpanIdx[i] : ~prevIdx;
            if (curIdx != prevIdx) {
                TextSpan piece;
                piece.start = runStart;
                piece.end = i;
                if (prevIdx >= 0) {
                    const auto& src = state.spans[prevIdx];
                    piece.style = src.style;
                    piece.appearance = src.appearance;
                    piece.hasRuby = src.hasRuby;
                    piece.rubyText = src.rubyText;
                    piece.hasUnderline = src.hasUnderline;
                    piece.hasStrikethrough = src.hasStrikethrough;
                    piece.isSuperscript = src.isSuperscript;
                    piece.isSubscript = src.isSubscript;
                    piece.yOffset = src.yOffset;
                } else {
                    piece.style = defaultStyle;
                    piece.appearance = defaultAppearance;
                }
                filled.push_back(piece);
                runStart = i;
                prevIdx = curIdx;
            }
        }

        state.spans = std::move(filled);
    }

    // 結果を構築
    ParseResult result;
    result.plainText = std::move(state.plainText);
    result.spans = std::move(state.spans);
    result.errors = std::move(state.errors);
    result.hasErrors = !result.errors.empty();

    // メタデータ
    result.timings = std::move(state.timings);
    result.links = std::move(state.links);
    result.graphics = std::move(state.graphics);

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
    // 親コンテキストから下線・取り消し線を継承
    bool hasUnderline = !state.styleStack.empty() ? state.styleStack.top().hasUnderline : false;
    bool hasStrikethrough = !state.styleStack.empty() ? state.styleStack.top().hasStrikethrough : false;
    bool isSuperscript = false;
    bool isSubscript = false;
    float yOffset = 0.0f;
    bool isVoidTag = false;  // 閉じタグ不要のタグ

    if (tagName == "font") {
        newStyle = applyFontTag(state.currentStyle, attrs);
    } else if (tagName == "b" || tagName == "strong") {
        if (options_.ignoreType) return true;
        newStyle = applyBoldTag(state.currentStyle);
    } else if (tagName == "i" || tagName == "em") {
        if (options_.ignoreType) return true;
        newStyle = applyItalicTag(state.currentStyle);
    } else if (tagName == "u") {
        hasUnderline = true;
    } else if (tagName == "s" || tagName == "strike" || tagName == "del") {
        hasStrikethrough = true;
    } else if (tagName == "sup") {
        newStyle = applySupTag(state.currentStyle);
        isSuperscript = true;
        yOffset = options_.supOffset * state.currentStyle.fontSize;
    } else if (tagName == "sub") {
        newStyle = applySubTag(state.currentStyle);
        isSubscript = true;
        yOffset = options_.subOffset * state.currentStyle.fontSize;
    } else if (tagName == "color") {
        if (options_.ignoreColor) return true;
        newAppearance = applyColorTag(state.currentAppearance, attrs);
    } else if (tagName == "outline") {
        if (options_.ignoreType) return true;
        newAppearance = applyOutlineTag(state.currentAppearance, attrs);
    } else if (tagName == "shadow") {
        if (options_.ignoreType) return true;
        newAppearance = applyShadowTag(state.currentAppearance, attrs);
    } else if (tagName == "ruby") {
        if (options_.ignoreRuby) return true;
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
        addPlainChar(state, u'\n');
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
            addPlainChar(state, u' ');
        }
        isVoidTag = true;
    // ------------------------------------------------------------------
    // 新規タグ: タイミング・リンク・グラフィック・アライン・ピッチ
    // ------------------------------------------------------------------
    } else if (tagName == "start") {
        isVoidTag = true;
        if (!options_.ignoreDelay) {
            TimingEntry entry;
            entry.type = TimingEntry::Type::Start;
            entry.charIndex = static_cast<int>(state.plainText.size());
            auto it = attrs.find("diff");
            if (it != attrs.end()) entry.startDiff = parseFloat(it->second);
            it = attrs.find("all");
            if (it != attrs.end()) entry.startAll = parseFloat(it->second);
            state.timings.push_back(entry);
        }
    } else if (tagName == "delay") {
        isVoidTag = true;
        if (!options_.ignoreDelay) {
            auto it = attrs.find("value");
            if (it != attrs.end()) {
                const auto& v = it->second;
                if (!v.empty() && v.back() == '%') {
                    // 末尾に % → パーセント指定
                    state.currentDelayPercent = parseFloat(v.substr(0, v.size() - 1));
                    state.currentDelayMs = -1.0f;
                } else {
                    // 数値のみ → ms 指定
                    state.currentDelayMs = parseFloat(v);
                }
            }
        }
    } else if (tagName == "wait") {
        isVoidTag = true;
        if (!options_.ignoreDelay) {
            TimingEntry entry;
            entry.type = TimingEntry::Type::Wait;
            entry.charIndex = static_cast<int>(state.plainText.size());
            auto it = attrs.find("value");
            if (it != attrs.end()) {
                const auto& v = it->second;
                if (!v.empty() && v.back() == '%') {
                    entry.waitPercent = parseFloat(v.substr(0, v.size() - 1));
                } else {
                    entry.waitMs = parseFloat(v);
                }
            }
            state.timings.push_back(entry);
        }
    } else if (tagName == "sync") {
        isVoidTag = true;
        if (!options_.ignoreDelay) {
            TimingEntry entry;
            entry.type = TimingEntry::Type::Sync;
            entry.charIndex = static_cast<int>(state.plainText.size());
            auto it = attrs.find("value");
            if (it != attrs.end()) {
                const auto& v = it->second;
                // 全部数値なら ms、そうでなければラベル
                bool allDigits = !v.empty();
                for (char c : v) {
                    if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-') {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits) {
                    entry.syncMs = parseFloat(v);
                } else if (labelResolver_) {
                    entry.syncMs = labelResolver_(v);
                }
            }
            state.timings.push_back(entry);
        }
    } else if (tagName == "keywait") {
        isVoidTag = true;
        if (!options_.ignoreDelay) {
            TimingEntry entry;
            entry.type = TimingEntry::Type::KeyWait;
            entry.charIndex = static_cast<int>(state.plainText.size());
            state.timings.push_back(entry);
        }
    } else if (tagName == "eval") {
        isVoidTag = true;
        auto nameIt = attrs.find("name");
        if (nameIt != attrs.end()) {
            std::u16string displayText;
            std::u16string nameU16 = utf8ToUtf16(nameIt->second);
            // コールバックで表示文字列を決定
            if (evalCallback_) {
                displayText = evalCallback_(nameU16);
            }
            // 空文字または未登録の場合は alt を使用
            if (displayText.empty()) {
                auto altIt = attrs.find("alt");
                if (altIt != attrs.end()) {
                    displayText = utf8ToUtf16(altIt->second);
                }
            }
            // alt も未定義なら name をそのまま表示
            if (displayText.empty()) {
                displayText = nameU16;
            }
            // 表示文字列をプレーンテキストに追加
            for (char16_t ch : displayText) {
                addPlainChar(state, ch);
            }
        }
    } else if (tagName == "link") {
        if (!options_.ignoreLink) {
            LinkInfo li;
            auto it = attrs.find("name");
            if (it != attrs.end()) li.name = it->second;
            li.startIndex = state.plainText.size();
            li.endIndex = state.plainText.size();
            state.links.push_back(li);
            state.currentLinkIndex = static_cast<int>(state.links.size()) - 1;
        }
        // container タグ: ignoreLink でもスタックに積んで閉じタグを正しく処理
    } else if (tagName == "graph") {
        isVoidTag = true;
        if (!options_.ignoreGraph) {
            GraphInfo gi;
            auto it = attrs.find("name");
            if (it != attrs.end()) gi.name = utf8ToUtf16(it->second);
            it = attrs.find("width");
            if (it != attrs.end()) gi.width = parseFloat(it->second);
            it = attrs.find("height");
            if (it != attrs.end()) gi.height = parseFloat(it->second);
            gi.charIndex = static_cast<int>(state.plainText.size());
            state.graphics.push_back(gi);
            // U+FFFC プレースホルダを挿入
            addPlainChar(state, u'\uFFFC');
        }
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
    }

    // link タグの閉じ処理
    if (top.tagName == "link") {
        state.currentLinkIndex = -1;
    }

    // スパンを追加（link タグはスタイル変更しないのでスパン不要）
    if (top.tagName != "link") {
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
    }

    state.styleStack.pop();

    // 親のスタイルに戻す
    if (!state.styleStack.empty()) {
        state.currentStyle = state.styleStack.top().style;
        state.currentAppearance = state.styleStack.top().appearance;
    } else {
        // スタックが空 → デフォルトに復元
        state.currentStyle = state.defaultStyle;
        state.currentAppearance = state.defaultAppearance;
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

    // 属性値を UTF-16 のまま収集してから UTF-8 に変換する
    std::u16string u16result;

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

        u16result += c;
        ++pos;
    }

    return utf16ToUtf8(u16result);
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
    if (it != attrs.end() && !options_.ignoreSize) {
        style.fontSize = parseFloat(it->second);
    }

    it = attrs.find("weight");
    if (it != attrs.end() && !options_.ignoreType) {
        style.fontWeight = static_cast<uint16_t>(parseInt(it->second));
    }

    it = attrs.find("spacing");
    if (it != attrs.end() && !options_.ignoreSpacing) {
        style.letterSpacing = parseFloat(it->second);
    }

    it = attrs.find("width");
    if (it != attrs.end() && !options_.ignoreType) {
        style.fontWidth = parseFloat(it->second);
    }

    it = attrs.find("face");
    if (it != attrs.end() && !it->second.empty() && !options_.ignoreFace) {
        auto& fm = FontManager::instance();
        // 名前付きコレクションを優先参照（フォールバックチェーン付き）
        auto collection = fm.getCollection(it->second);
        if (!collection) {
            // フォールバック: 単一フォント名でコレクション作成
            collection = fm.createCollection({it->second});
        }
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
    uint32_t color = 0xFFFFFFFF;
    float ox = 0, oy = 0;

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

    it = attrs.find("x");
    if (it != attrs.end()) ox = parseFloat(it->second);

    it = attrs.find("y");
    if (it != attrs.end()) oy = parseFloat(it->second);

    Appearance app = current;
    bool addMode = (attrs.find("add") != attrs.end());
    if (addMode) {
        app.addColor(color, ox, oy);
    } else {
        app.setColor(color);
    }
    return app;
}

Appearance TagParser::applyOutlineTag(const Appearance& current,
                                      const std::map<std::string, std::string>& attrs) {
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

    Appearance app = current;
    bool addMode = (attrs.find("add") != attrs.end());
    if (addMode) {
        app.addOutline(color, width, ox, oy);
    } else {
        app.setOutline(color, width, ox, oy);
    }
    return app;
}

Appearance TagParser::applyShadowTag(const Appearance& current,
                                     const std::map<std::string, std::string>& attrs) {
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

    Appearance app = current;
    bool addMode = (attrs.find("add") != attrs.end());
    if (addMode) {
        app.addShadow(color, ox, oy);
    } else {
        app.setShadow(color, ox, oy);
    }
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
