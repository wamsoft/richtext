# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 概要

多言語・装飾対応のリッチテキストレンダリングライブラリ。minikin によるテキストレイアウト、FreeType によるグリフパス化、thorvg によるベクター描画を組み合わせる。

吉里吉里プラグインとしてのバインディングは `../krkr_richtext` に分離されている。

## ビルド

事前準備として `VCPKG_ROOT` 環境変数を設定する必要がある。

```bash
# CMake 構成（初回またはCMakeLists.txt変更時）
make prebuild                          # OS自動検出でプリセット選択
make prebuild PRESET=x64-windows       # プリセットを明示指定

# ビルド
make build
make build BUILD_TYPE=Debug            # デバッグビルド

# サンプル実行（BMP ファイルが output.bmp に出力される）
cd build/x64-windows/Release
./sample_render.exe
```

プリセット: `x86-windows`, `x64-windows`, `x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx`, `arm64-android`, `x64-android`

## アーキテクチャ

### レイヤー構造（下から上へ）

```
タグ解析層           src/TagParser.cpp      HTMLライクなタグ→スタイル区間変換
描画層               src/TextRenderer.cpp   描画統合インタフェース
                     src/GlyphRenderer.cpp  グリフ単位描画・キャッシング
レイアウト層         src/ParagraphLayout.cpp 複数行レイアウト（行分割）
                     src/TextLayout.cpp      1行レイアウト（minikin::Layout）
スタイル管理層       src/TextStyle.cpp       minikin::MinikinPaint 設定
                     src/Appearance.cpp      DrawStyle（塗り/ストローク）
フォント管理層       src/FontFace.cpp        FreeType ↔ minikin 橋渡し
                     src/FontManager.cpp     フォント登録・シングルトン管理
```

### データフロー

1. `FontManager` がフォントを FreeType で読み込み、`FontFace`（`minikin::MinikinFont` 継承）として管理
2. `TextLayout` / `ParagraphLayout` が minikin でグリフ配置を計算
3. `GlyphRenderer` が `FontFace::getGlyphPath()` で FreeType アウトラインを取得し thorvg パスに変換して描画
4. カラー絵文字は `FontFace::getGlyphBitmap()` でビットマップとして取得（パスとは別処理）
5. `TagParser` はタグ付きテキストを解析して `StyleRun` 配列に変換し、`TextRenderer` に渡す

### 外部ライブラリ

- `ext/minikin` — テキストレイアウト・行分割（ICU + harfbuzz を内包、git submodule）
- `ext/thorvg` — ベクターグラフィックス描画（SW エンジンのみ使用、git submodule、cmake ブランチ）
- FreeType / zlib / libpng — vcpkg でインストール

### ビルド成果物

- `richtext_lib` — 静的ライブラリ（コア機能）
- `sample_render.exe` — 動作確認サンプル
- `sample_sequential.exe` — 逐次表示サンプル

### 参考ドキュメント

- `設計.md` — クラス設計・詳細仕様
- `実装.md` — フェーズ別実装進捗
