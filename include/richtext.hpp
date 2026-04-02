#ifndef RICHTEXT_HPP
#define RICHTEXT_HPP

/**
 * RichText ライブラリ
 * 
 * 吉里吉里用リッチテキストレンダリングライブラリ
 * 
 * 主要コンポーネント:
 * - FontManager: フォント管理（登録・解除）
 * - FontFace: フォントフェイス（FreeType連携）
 * - TextStyle: テキストスタイル定義
 * - Appearance: 描画外観定義
 * - TextLayout: 1行テキストレイアウト
 * - ParagraphLayout: 複数行テキストレイアウト
 * - TextRenderer: 描画処理
 * - TagParser: タグ付きテキストの解析
 */

#include "richtext/FontManager.hpp"
#include "richtext/FontFace.hpp"
#include "richtext/TextStyle.hpp"
#include "richtext/Appearance.hpp"
#include "richtext/TextLayout.hpp"
#include "richtext/ParagraphLayout.hpp"
#include "richtext/StyledLayout.hpp"
#include "richtext/TextRenderer.hpp"
#include "richtext/TagParser.hpp"
#include "richtext/TextureAtlas.hpp"
#include "richtext/TimingInfo.hpp"
#include "richtext/LinkRegion.hpp"

#endif // RICHTEXT_HPP
