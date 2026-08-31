# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 概要

多言語・装飾対応のリッチテキストレンダリングライブラリ。minikin によるテキストレイアウト、glyphware（FreeType）によるグリフのラスタライズ、自前のマスク合成による描画を組み合わせる。

吉里吉里プラグインとしてのバインディングは `../krkr_richtext` に分離されている。

## ビルド

事前準備として `VCPKG_ROOT` 環境変数を設定する必要がある。
また、フォントバックエンド（glyphware）のソースツリーを `GLYPHWARE_DIR` で指す。

```bash
# CMake 構成（初回またはCMakeLists.txt変更時）
make prebuild                          # OS自動検出でプリセット選択
make prebuild PRESET=x64-windows       # プリセットを明示指定
make prebuild CMAKEOPT=-DGLYPHWARE_DIR=/path/to/glyphware

# ビルド
make build
make build BUILD_TYPE=Debug            # デバッグビルド

# サンプル実行（リポジトリルートで実行すること。フォントを ./data/ から読む）
./build/x64-windows/Release/sample_render.exe
```

`GLYPHWARE_DIR` を与えない場合 `RICHTEXT_USE_GLYPHWARE` が定義されず、既定
バックエンドが無い状態でビルドされる。その場合は利用側が
`FontManager::setFontBackend()` でバックエンドを注入しないとフォントを開けない
（吉里吉里プラグインのように本体のフォントエンジンを共有する構成向け）。

プリセット: `x86-windows`, `x64-windows`, `x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx`, `arm64-android`, `x64-android`

## アーキテクチャ

### レイヤー構造（下から上へ）

```
タグ解析層           src/TagParser.cpp      HTMLライクなタグ→スタイル区間変換
描画層               src/TextRenderer.cpp   描画統合インタフェース
                     src/GlyphRenderer.cpp  グリフ単位描画・マスクキャッシング
                     src/Raster.cpp         ARGB8888 への合成（マスク/矩形/画像）
レイアウト層（横組み） src/StyledLayout.cpp    タグ付きテキストのレイアウト（タグ解析+行分割+セグメント構築）
                     src/ParagraphLayout.cpp 複数行レイアウト（行分割）
                     src/TextLayout.cpp      1行レイアウト（minikin::Layout）
レイアウト層（縦組み） src/vertical/VerticalParagraphLayout.cpp 縦組み段落（行分割＋配置）
                     src/vertical/LineBreaker.cpp    自前行分割（Greedy / Knuth-Plass）
                     src/vertical/LineItemBuilder.cpp クラスタ列→Box/Glue/Penalty 列
                     src/vertical/SpacingTable.cpp   JLReq 表3 のアキ量
                     src/vertical/CharClass.cpp      JLReq 文字クラス・禁則
                     src/vertical/VerticalLayout.cpp 縦1行レイアウト（ベタ組み）
                     src/vertical/VerticalShaper.cpp 縦組みシェイピング（HarfBuzz TTB 直叩き）
                     src/vertical/WritingMode.cpp    書字方向・座標系・UAX#50 相当の向き判定
テクスチャ管理層     src/TextureAtlas.cpp    グリフのテクスチャアトラス管理
スタイル管理層       src/TextStyle.cpp       minikin::MinikinPaint 設定
                     src/Appearance.cpp      DrawStyle（塗り/ストローク）
フォント管理層       src/FontFace.cpp        FontBackend ↔ minikin 橋渡し
                     src/FontManager.cpp     フォント登録・シングルトン管理
グリフ供給層         include/richtext/FontBackend.hpp   差し替え口（抽象）
                     src/GlyphwareBackend.cpp          既定実装（glyphware 直リンク）
```

### フォントバックエンド

グリフの実体供給（メトリクス・アウトライン・ビットマップ・バリアブル軸）は
`FontBackend` / `FontBackendFace` 越しに行う。実装は 2 系統:

1. **自前初期化** — `GlyphwareFontBackend`（`RICHTEXT_USE_GLYPHWARE` 時の既定）。
   `FontManager` に登録された `FontDataLoader` からバイト列を取り `glyphware::Face` を開く
2. **ホスト注入** — 吉里吉里本体の統一フォントエンジンをプラグインが
   `FontServiceIntf` 経由で包んだものを `FontManager::setFontBackend()` で差し込む。
   本体・thorvg・richtext で FT_Face とフォントバイト列が共有される

カラー絵文字は 2 系統ある。CBDT/sbix のビットマップは
`getGlyphBitmap()`、COLR (v0/v1) のベクター定義は `getColorLayers()` で
「アウトライン + 変換 + 塗り」のレイヤー列に展開してもらい、richtext 側で
レイヤーごとにマスクを作って合成する（`FontFace::renderCOLRv1Glyph`）。

### 描画

ベクターの塗り／縁取りは `FontBackendFace::getGlyphMask()` が返す 8bit
カバレッジマスクを `RenderTarget` へ合成して行う（`src/Raster.cpp`）。
外部のベクターグラフィックスエンジンには依存しない。
サイズ・斜体シアー・幅スケール・サブピクセル位置・上下反転・ユーザ変換は
1 つのアフィン行列に畳み込んでアウトラインに焼き込むため、後段でのスケール
による劣化やずれが無い。マスクは変形の 2x2 成分と 1/4px のサブピクセル位相を
キーにキャッシュされる。

将来ベクターエンジン（thorvg 等）を使いたくなった場合の差し替え口は
`GlyphRenderer::blendGlyph()` の 1 箇所。手順と注意点は `設計.md` の
「6.1 描画バックエンドの差し替え」、thorvg を使う場合に
アウトライン抽出を二重化しないための設計は「6.1.1」に書いてある。

### 縦組み

縦組みは横組み経路とは別系統で、`include/richtext/vertical/` に置く。
minikin のシェイピングは LTR / RTL しか扱えない（`LayoutCore.cpp` が
`HB_DIRECTION_LTR` / `RTL` しか設定しない）ため、`minikin::Layout` は通さず、
`FontCollection::itemize()` によるフォントフォールバック解決だけ再利用して
HarfBuzz を直接叩く。

- 正立ラン（和文）は `HB_DIRECTION_TTB`。`vert` / `vrt2` の縦字形置換、
  `vmtx` / `VORG` の縦アドバンスと原点補正、`vkrn` / `vpal` が HarfBuzz 側で効く
- 横倒しラン（欧文）は `HB_DIRECTION_LTR` で組んでから列へ 90 度倒す
- 座標系は `WritingMode.hpp` 参照。`GlyphInfo::x` = 縦ベースライン（列の中心線）
  からの左右、`GlyphInfo::y` = 行頭からの送り
- グリフの回転・スケールは `GlyphInfo::rotation` / `scaleX` / `scaleY` で渡し、
  `GlyphRenderer` が既存のアフィン行列へ畳み込む

和文組版そのものは「グリフ配置の問題」ではなく **文字クラス間のアキ（グルー）と
ペナルティの列を解く問題** として扱う。1 文字＝1em の箱を並べる方式では約物の詰め・
禁則・追い込み／追い出しが原理的に表現できないため。

- `CharClass` が JLReq の文字クラスへ分類し、`SpacingTable` が隣接ペアの
  アキ量（自然値・伸び・縮み）を返す
- `LineItemBuilder` が Box / Glue / Penalty 列へ変換する。約物は仮想ボディを
  半角へ詰め、禁則は Glue の直前の Penalty(∞)、ぶら下げは幅が負の Penalty で表す
- `LineBreaker`（自前・minikin のものは使わない）が行を決め、グルーの調整比を返す。
  追い込み・追い出し・両端揃えはこの 1 つの比で表現される
- `VerticalParagraphLayout` が段落全体をまとめ、行ごとの `GlyphInfo` 列を作る

進捗とフェーズ計画は `縦組み設計.md` と `実装.md` を見ること。

### データフロー

1. 利用側が `FontManager` にローダー関数（またはバックエンド）を登録し、`registerFont()` でフォントを登録。バックエンドが face を開き、`FontFace`（`minikin::MinikinFont` 継承）として管理
2. `TextLayout` / `ParagraphLayout` が minikin でグリフ配置を計算（minikin 自身は生バイト列 + 自前 harfbuzz でシェイピングする）
3. `GlyphRenderer` がバックエンドにカバレッジマスクを作らせ（変形はアウトラインに焼き込み済み）、`RenderTarget` へ色を乗せて合成
4. カラー絵文字は `FontFace::getGlyphBitmap()` でビットマップとして取得（パスとは別処理）
5. `TagParser` はタグ付きテキストを解析して `StyleRun` 配列に変換し、`StyledLayout` または `TextRenderer` に渡す
6. `StyledLayout` はタグ解析・行分割・セグメント構築を一括実行し、レイアウト結果を保持。`TextRenderer::drawStyledLayout()` で描画、`TextureAtlas::getCopyRects()` でコピー矩形生成に利用

### 外部ライブラリ

- `ext/minikin` — テキストレイアウト・行分割（ICU + harfbuzz を内包、git submodule）
- glyphware — 統一フォントエンジン（FreeType + HarfBuzz、`GLYPHWARE_DIR` で外部ツリーを指定）。
  吉里吉里本体・thorvg gw ローダーと共通のフォント実体を扱うためのライブラリ
- FreeType / zlib / libpng — vcpkg でインストール

### ビルド成果物

- `richtext_lib` — 静的ライブラリ（コア機能）
- `sample_render.exe` — 動作確認サンプル（**リポジトリルートから実行**すること）
- `sample_sequential.exe` — 逐次表示サンプル（ParagraphLayout / StyledLayout）
- `sample_texture.exe` — テクスチャアトラスサンプル（ParagraphLayout / StyledLayout）
- `sample_vertical.exe` — 縦組みサンプル（VerticalLayout）

### 参考ドキュメント

- `設計.md` — クラス設計・詳細仕様
- `縦組み設計.md` — 縦組み（JLReq 水準）の設計とフェーズ計画
- `実装.md` — フェーズ別実装進捗
