# richtext

多言語・装飾対応のリッチテキストレンダリングライブラリ。

## 使用外部ライブラリ

- `ext/minikin` — パラグラフレンダリング処理を実現するライブラリ
- `ext/thorvg` — パスのベクター描画用のライブラリ
- FreeType / zlib / libpng — vcpkg でインストール

## ビルド成果物

- `richtext_lib` — 静的ライブラリ（コア機能）
- `sample_render.exe` — 動作確認サンプル

## 関連プロジェクト

- `../krkr_richtext` — 吉里吉里プラグイン（TJS バインディング）
