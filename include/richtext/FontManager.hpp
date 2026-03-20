#ifndef RICHTEXT_FONT_MANAGER_HPP
#define RICHTEXT_FONT_MANAGER_HPP

#include <string>
#include <memory>
#include <map>
#include <vector>

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// minikin
#include <minikin/FontCollection.h>

namespace richtext {

class FontFace;

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
    // フォント管理
    // ------------------------------------------------------------------
    
    /**
     * フォント登録
     * @param path フォントファイルパス
     * @param name 登録名
     * @param index フォントインデックス（OTCの場合）
     * @return 成功時 true
     */
    bool registerFont(const std::string& path, 
                      const std::string& name,
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
    
    std::map<std::string, std::shared_ptr<FontFace>> fonts_;
    std::map<std::string, uint32_t> localeIds_;
};

} // namespace richtext

#endif // RICHTEXT_FONT_MANAGER_HPP
