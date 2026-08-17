/**
 * FontManager.cpp
 * 
 * フォント管理クラス（シングルトン）の実装
 */

#include "richtext/FontManager.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/GlyphwareBackend.hpp"

#include <minikin/Font.h>
#include <minikin/FontFamily.h>
#include <minikin/FontCollection.h>
#include <minikin/LocaleList.h>

#include <cstdio>
#include <stdexcept>

namespace richtext {

// ----------------------------------------------------------------------------
// FontManager 実装
// ----------------------------------------------------------------------------

FontManager& FontManager::instance() {
    static FontManager instance;
    return instance;
}

FontManager::FontManager()
    : initialized_(false)
{
}

FontManager::~FontManager() {
    terminate();
}

bool FontManager::initialize() {
    if (initialized_) {
        return true;
    }

    // バックエンド未注入なら既定（glyphware）を用意する
    if (!backend_) {
#ifdef RICHTEXT_USE_GLYPHWARE
        // ローダーはここで束ねずに呼び出し時に引くこと。
        // initialize() の後に setFontDataLoader() されても効くようにするため。
        backend_ = std::make_shared<GlyphwareFontBackend>(
            [](const std::string& name) -> FontDataBuffer {
                return FontManager::instance().loadFontBytes(name);
            });
#else
        fprintf(stderr, "No font backend: build with RICHTEXT_USE_GLYPHWARE "
                        "or call setFontBackend() before initialize()\n");
        return false;
#endif
    }

    initialized_ = true;
    return true;
}

void FontManager::terminate() {
    // コレクション → フォントの順にクリア（参照関係を考慮）
    collections_.clear();
    fonts_.clear();
    localeIds_.clear();

    // ローダー / バックエンドもクリア
    dataLoader_ = nullptr;
    backend_.reset();

    initialized_ = false;
}

void FontManager::setFontBackend(std::shared_ptr<FontBackend> backend) {
    backend_ = std::move(backend);
}

void FontManager::setFontDataLoader(FontDataLoader loader) {
    dataLoader_ = std::move(loader);
}

FontDataBuffer FontManager::loadFontBytes(const std::string& name) const {
    if (!dataLoader_) return nullptr;
    auto data = dataLoader_(name);
    if (!data || data->empty()) return nullptr;
    return data;
}

std::shared_ptr<FontFace> FontManager::loadFontFace(const std::string& fileName, int index) {
    if (!backend_) return nullptr;
    auto face = backend_->openFace(fileName, index);
    if (!face) return nullptr;
    return std::make_shared<FontFace>(fileName, std::move(face), index);
}

bool FontManager::registerFont(const std::string& fileName,
                               const std::string& name,
                               int index) {
    if (!initialized_) {
        if (!initialize()) {
            return false;
        }
    }

    // 既に登録済みなら上書き
    fonts_.erase(name);

    try {
        auto fontFace = loadFontFace(fileName, index);
        if (!fontFace) {
            fprintf(stderr, "Failed to open font '%s'\n", fileName.c_str());
            return false;
        }
        FontEntry entry;
        entry.face = fontFace;
        entry.weight = 400;
        entry.slant = minikin::FontStyle::Slant::UPRIGHT;
        fonts_[name].push_back(std::move(entry));
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to register font '%s': %s\n", name.c_str(), e.what());
        return false;
    }
}

bool FontManager::registerVariableFont(const std::string& fileName,
                                        const std::string& name,
                                        uint16_t weight,
                                        bool italic,
                                        int index) {
    if (!initialized_) {
        if (!initialize()) {
            return false;
        }
    }

    try {
        auto fontFace = loadFontFace(fileName, index);
        if (!fontFace) {
            fprintf(stderr, "Failed to open font '%s'\n", fileName.c_str());
            return false;
        }
        if (!fontFace->isVariableFont()) {
            fprintf(stderr, "Font '%s' is not a variable font\n", fileName.c_str());
            return false;
        }

        // wght 軸 + ital 軸を設定
        std::vector<minikin::FontVariation> variations;
        variations.emplace_back(0x77676874 /* wght */, static_cast<float>(weight));
        if (italic) {
            variations.emplace_back(0x6974616C /* ital */, 1.0f);
        }
        fontFace->setVariations(variations);

        FontEntry entry;
        entry.face = fontFace;
        entry.weight = weight;
        entry.slant = italic ? minikin::FontStyle::Slant::ITALIC
                             : minikin::FontStyle::Slant::UPRIGHT;
        fonts_[name].push_back(std::move(entry));
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to register variable font '%s': %s\n", name.c_str(), e.what());
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
    // 登録名で完全一致
    auto it = fonts_.find(name);
    if (it != fonts_.end() && !it->second.empty()) {
        return it->second[0].face;
    }
    // フォントファイルの family / "family Style" でフォールバック検索
    for (const auto& kv : fonts_) {
        for (const auto& entry : kv.second) {
            if (!entry.face) continue;
            const auto& fam = entry.face->getFamilyName();
            const auto& sty = entry.face->getStyleName();
            if (!fam.empty() && fam == name) return entry.face;
            if (!fam.empty() && !sty.empty() &&
                fam.size() + 1 + sty.size() == name.size() &&
                name.compare(0, fam.size(), fam) == 0 &&
                name[fam.size()] == ' ' &&
                name.compare(fam.size() + 1, sty.size(), sty) == 0) {
                return entry.face;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<minikin::FontCollection> FontManager::createCollection(
    const std::vector<std::string>& names) {

    if (names.empty()) {
        return nullptr;
    }

    auto matchesFaceName = [](const FontEntry& entry, const std::string& query) {
        if (!entry.face) return false;
        const auto& fam = entry.face->getFamilyName();
        const auto& sty = entry.face->getStyleName();
        if (!fam.empty() && fam == query) return true;
        if (!fam.empty() && !sty.empty() &&
            fam.size() + 1 + sty.size() == query.size() &&
            query.compare(0, fam.size(), fam) == 0 &&
            query[fam.size()] == ' ' &&
            query.compare(fam.size() + 1, sty.size(), sty) == 0) {
            return true;
        }
        return false;
    };

    std::vector<std::shared_ptr<minikin::FontFamily>> families;

    for (const auto& name : names) {
        std::vector<minikin::Font> fonts;

        // 1. 登録名で完全一致
        auto it = fonts_.find(name);
        if (it != fonts_.end() && !it->second.empty()) {
            for (const auto& entry : it->second) {
                fonts.push_back(
                    minikin::Font::Builder(entry.face)
                        .setWeight(entry.weight)
                        .setSlant(entry.slant)
                        .build());
            }
        } else {
            // 2. フォントファイル内の family / "family Style" 名でフォールバック
            for (const auto& kv : fonts_) {
                for (const auto& entry : kv.second) {
                    if (!matchesFaceName(entry, name)) continue;
                    fonts.push_back(
                        minikin::Font::Builder(entry.face)
                            .setWeight(entry.weight)
                            .setSlant(entry.slant)
                            .build());
                }
            }
        }

        if (fonts.empty()) {
            fprintf(stderr, "Font not found: %s\n", name.c_str());
            continue;
        }

        auto family = std::make_shared<minikin::FontFamily>(std::move(fonts));
        families.push_back(family);
    }

    if (families.empty()) {
        return nullptr;
    }

    return std::make_shared<minikin::FontCollection>(families);
}

bool FontManager::registerCollection(const std::string& collectionName,
                                     const std::vector<std::string>& fontNames) {
    auto collection = createCollection(fontNames);
    if (!collection) {
        return false;
    }
    collections_[collectionName] = collection;
    return true;
}

std::shared_ptr<minikin::FontCollection> FontManager::getCollection(
    const std::string& collectionName) const {
    auto it = collections_.find(collectionName);
    if (it == collections_.end()) {
        return nullptr;
    }
    return it->second;
}

bool FontManager::unregisterCollection(const std::string& collectionName) {
    return collections_.erase(collectionName) > 0;
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
