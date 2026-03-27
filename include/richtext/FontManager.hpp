#ifndef RICHTEXT_FONT_MANAGER_HPP
#define RICHTEXT_FONT_MANAGER_HPP

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// minikin
#include <minikin/FontCollection.h>
#include <minikin/FontStyle.h>

namespace richtext {

class FontFace;

/**
 * フォントデータバッファ型
 */
using FontDataBuffer = std::shared_ptr<std::vector<uint8_t>>;

/**
 * フォントデータローダー（バッファ方式）
 * フォント名を受け取り、フォントデータ全体を shared_ptr<vector<uint8_t>> で返す。
 * 読み込み失敗時は nullptr を返す。
 */
using FontDataLoader = std::function<FontDataBuffer(const std::string& name)>;

/**
 * FreeType ストリームローダー
 * フォント名を受け取り、FT_Stream を返す。
 * 呼び出し元が FT_Stream の寿命管理を行う。
 * 読み込み失敗時は nullptr を返す。
 */
using FontStreamLoader = std::function<FT_Stream(const std::string& name)>;

/**
 * フォント管理クラス（シングルトン）
 *
 * フォントの登録・解除、FontCollection の作成、ロケール管理を行う。
 */
class FontManager {
public:
    /**
     * シングルトンインスタンス取得
     */
    static FontManager& instance();

    /**
     * 初期化（FreeType ライブラリの初期化）
     * @return 成功時 true
     */
    bool initialize();

    /**
     * 終了処理
     */
    void terminate();
    
    /**
     * FreeType ライブラリの取得
     */
    FT_Library getFTLibrary() const { return ftLibrary_; }

    // ------------------------------------------------------------------
    // ローダー登録
    // ------------------------------------------------------------------

    /**
     * フォントデータローダー（バッファ方式）を登録
     * ストリームローダーより優先される。
     */
    void setFontDataLoader(FontDataLoader loader);

    /**
     * FreeType ストリームローダーを登録
     * バッファローダーが未登録、または nullptr を返した場合に使用される。
     */
    void setFontStreamLoader(FontStreamLoader loader);

    // ------------------------------------------------------------------
    // フォント管理
    // ------------------------------------------------------------------

    /**
     * フォント登録
     * @param fileName フォントファイル名（ローダーに渡される）
     * @param name 登録名（createCollection 等で使用）
     * @param index フォントインデックス（OTCの場合）
     * @return 成功時 true
     */
    bool registerFont(const std::string& fileName,
                      const std::string& name,
                      int index = 0);

    /**
     * バリアブルフォント登録（ウェイト・イタリック指定）
     *
     * 同じフォントファイルからウェイト/スタイル別の FontFace を作成して登録する。
     * minikin のフォント選択で適切なウェイト・スタイルが選ばれるようになる。
     *
     * @param fileName フォントファイル名（ローダーに渡される）
     * @param name 登録名（createCollection 等で使用）
     * @param weight フォントウェイト（100-900）
     * @param italic イタリック（true で ital 軸=1 + ITALIC slant 登録）
     * @param index フォントインデックス（OTCの場合）
     * @return 成功時 true
     */
    bool registerVariableFont(const std::string& fileName,
                              const std::string& name,
                              uint16_t weight,
                              bool italic = false,
                              int index = 0);
    
    /**
     * フォント解除
     * @param name 登録名
     * @return 成功時 true
     */
    bool unregisterFont(const std::string& name);
    
    /**
     * 登録済みフォントの取得
     * @param name 登録名
     * @return FontFace のポインタ（未登録時 nullptr）
     */
    std::shared_ptr<FontFace> getFont(const std::string& name) const;
    
    /**
     * フォントコレクション作成
     * @param names フォント名のリスト（優先度順）
     * @return minikin::FontCollection の shared_ptr
     */
    std::shared_ptr<minikin::FontCollection> createCollection(
        const std::vector<std::string>& names);

    /**
     * 名前付きフォントコレクション登録
     *
     * フォント名リストからコレクションを作成し、名前を付けて登録する。
     * タグパーサーの <font face='...'> で名前指定するとこのコレクションが使用される。
     *
     * @param collectionName コレクション名（例: "sans", "serif"）
     * @param fontNames フォント名のリスト（優先度順、フォールバックチェーン）
     * @return 成功時 true
     */
    bool registerCollection(const std::string& collectionName,
                            const std::vector<std::string>& fontNames);

    /**
     * 名前付きフォントコレクション取得
     * @param collectionName コレクション名
     * @return コレクション（未登録時 nullptr）
     */
    std::shared_ptr<minikin::FontCollection> getCollection(
        const std::string& collectionName) const;

    /**
     * 名前付きフォントコレクション解除
     * @param collectionName コレクション名
     * @return 成功時 true
     */
    bool unregisterCollection(const std::string& collectionName);
    
    // ------------------------------------------------------------------
    // ロケール管理
    // ------------------------------------------------------------------
    
    /**
     * ロケール登録
     * @param locale ロケール文字列（例: "ja_JP-u-lb-strict"）
     * @return ロケールID
     */
    uint32_t registerLocale(const std::string& locale);
    
    /**
     * 登録済みロケールIDの取得
     * @param locale ロケール文字列
     * @return ロケールID（未登録時 0）
     */
    uint32_t getLocaleId(const std::string& locale) const;

private:
    FontManager();
    ~FontManager();
    
    // コピー・ムーブ禁止
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    FontManager(FontManager&&) = delete;
    FontManager& operator=(FontManager&&) = delete;
    
    FT_Library ftLibrary_ = nullptr;
    bool initialized_ = false;

    FontDataLoader dataLoader_;
    FontStreamLoader streamLoader_;

    struct FontEntry {
        std::shared_ptr<FontFace> face;
        uint16_t weight = 400;
        minikin::FontStyle::Slant slant = minikin::FontStyle::Slant::UPRIGHT;
    };
    std::map<std::string, std::vector<FontEntry>> fonts_;
    std::map<std::string, std::shared_ptr<minikin::FontCollection>> collections_;
    std::map<std::string, uint32_t> localeIds_;

    /**
     * ローダーを使って FontFace を生成
     */
    std::shared_ptr<FontFace> loadFontFace(const std::string& name, int index);
};

} // namespace richtext

#endif // RICHTEXT_FONT_MANAGER_HPP
