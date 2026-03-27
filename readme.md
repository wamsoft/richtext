# richtext

多言語・装飾対応のリッチテキストレンダリングライブラリ。

minikin によるテキストレイアウト、FreeType によるグリフパス化、thorvg によるベクター描画を組み合わせて、高品質なテキスト描画を実現する。

## 主要機能

- 1行 / 複数行テキスト描画（左揃え・中央・右揃え）
- 多言語対応（CJK、RTL、ロケール別字形）
- フォントフォールバック
- カラー絵文字（CBDT / COLRv1）
- インラインタグによるスタイル変更
- ストローク（縁取り）・影の装飾
- バリアブルフォント（wght / ital / wdth 軸）
- ルビ（振り仮名）
- 逐次表示（文字送り）
- GPU テクスチャアトラス

## ビルド

事前準備として `VCPKG_ROOT` 環境変数を設定する。

```bash
# テスト用フォントのダウンロード
make fontdata

# CMake 構成
make prebuild

# ビルド
make build

# サンプル実行
# 直下の data フォルダ参照するのでこのフォルダで実行してください
build/x64-windows/Release/sample_render.exe
```

## ビルド成果物

- `richtext_lib` — 静的ライブラリ（コア機能）
- `sample_render.exe` — 動作確認サンプル
- `sample_sequential.exe` — 逐次表示サンプル
- `sample_texture.exe` - テクスチャグリフ展開サンプル

## ライセンス

本ライブラリは MIT License の下で提供される。詳細は [LICENSE](LICENSE) を参照。

### 依存ライブラリのライセンス

| ライブラリ | ライセンス | 管理方法 |
|-----------|-----------|---------|
| [minikin](ext/minikin/) | Apache License 2.0 | git submodule |
| [thorvg](ext/thorvg/) | MIT License | git submodule |
| [FreeType](https://freetype.org/) | FreeType License (BSD-like) | vcpkg |
| [HarfBuzz](https://harfbuzz.github.io/) | MIT License | minikin に内包 |
| [ICU](https://icu.unicode.org/) | Unicode License | minikin に内包 |
| [zlib](https://zlib.net/) | zlib License | vcpkg |
| [libpng](http://www.libpng.org/) | libpng License | vcpkg |
