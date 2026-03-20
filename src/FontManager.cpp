/**
 * FontManager.cpp
 * 
 * フォント管理クラス（シングルトン）の実装
 */

#include "richtext/FontManager.hpp"
#include "richtext/FontFace.hpp"

#include <minikin/Font.h>
#include <minikin/FontFamily.h>
#include <minikin/FontCollection.h>
#include <minikin/LocaleList.h>

#include <cstdio>

namespace richtext {

// ----------------------------------------------------------------------------
// FontManager 実装
// ----------------------------------------------------------------------------

FontManager& FontManager::instance() {
    static FontManager instance;
    return instance;
}

FontManager::FontManager()
    : ftLibrary_(nullptr)
    , initialized_(false)
{
}

FontManager::~FontManager() {
    terminate();
}

bool FontManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    FT_Error err = FT_Init_FreeType(&ftLibrary_);
    if (err != 0) {
        fprintf(stderr, "Failed to initialize FreeType library\n");
        return false;
    }
    
    initialized_ = true;
    return true;
}

void FontManager::terminate() {
    // フォントを先にクリア
    fonts_.clear();
    localeIds_.clear();
    
    if (ftLibrary_) {
        FT_Done_FreeType(ftLibrary_);
        ftLibrary_ = nullptr;
    }
    
    initialized_ = false;
}

bool FontManager::registerFont(const std::string& path, 
                               const std::string& name,
                               int index) {
    if (!initialized_) {
        if (!initialize()) {
            return false;
        }
    }
    
    // 既に登録済みなら上書き
    auto it = fonts_.find(name);
    if (it != fonts_.end()) {
        fonts_.erase(it);
    }
    
    try {
        auto fontFace = std::make_shared<FontFace>(path, index);
        fonts_[name] = fontFace;
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to register font '%s': %s\n", name.c_str(), e.what());
        return false;
    }
}

bool FontManager::unregisterFont(const std::string& name) {
    auto it = fonts_.find(name);
    if (it == fonts_.end()) {
        return false;
    }
    
    fonts_.erase(it);
    return true;
}

std::shared_ptr<FontFace> FontManager::getFont(const std::string& name) const {
    auto it = fonts_.find(name);
    if (it == fonts_.end()) {
        return nullptr;
    }
    return it->second;
}

std::shared_ptr<minikin::FontCollection> FontManager::createCollection(
    const std::vector<std::string>& names) {
    
    if (names.empty()) {
        return nullptr;
    }
    
    std::vector<std::shared_ptr<minikin::FontFamily>> families;
    
    for (const auto& name : names) {
        auto fontFace = getFont(name);
        if (!fontFace) {
            fprintf(stderr, "Font not found: %s\n", name.c_str());
            continue;
        }
        
        // Font を作成
        std::vector<minikin::Font> fonts;
        fonts.push_back(minikin::Font::Builder(fontFace).build());
        
        // FontFamily を作成
        auto family = std::make_shared<minikin::FontFamily>(std::move(fonts));
        families.push_back(family);
    }
    
    if (families.empty()) {
        return nullptr;
    }
    
    // FontCollection を作成
    return std::make_shared<minikin::FontCollection>(families);
}

uint32_t FontManager::registerLocale(const std::string& locale) {
    // 既に登録済みならそのIDを返す
    auto it = localeIds_.find(locale);
    if (it != localeIds_.end()) {
        return it->second;
    }
    
    // minikin に登録
    uint32_t id = minikin::registerLocaleList(locale);
    localeIds_[locale] = id;
    
    return id;
}

uint32_t FontManager::getLocaleId(const std::string& locale) const {
    auto it = localeIds_.find(locale);
    if (it == localeIds_.end()) {
        return 0;
    }
    return it->second;
}

} // namespace richtext
