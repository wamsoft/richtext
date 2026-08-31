#ifndef RICHTEXT_PDF_SFNT_INFO_HPP
#define RICHTEXT_PDF_SFNT_INFO_HPP

#include <cstddef>
#include <cstdint>

/**
 * SfntInfo — フォントファイル（sfnt）から PDF の FontDescriptor に必要な値を拾う
 *
 * PDF へ埋め込むときに、アウトラインが CFF か glyf かで
 * CIDFontType0 / CIDFontType2 の別と FontFile3 / FontFile2 の別が決まる。
 * FreeType を経由せずテーブルディレクトリを直接読むのは、PDF 側で必要な値が
 * head / hhea / post / OS/2 の生の値そのものだからで、変換を挟むと丸めが入る。
 */
namespace richtext::pdf {

struct SfntInfo {
    bool valid = false;
    bool isCFF = false;         ///< true なら CFF（OpenType/CFF）、false なら glyf
    bool isCollection = false;  ///< TTC。PDF へはそのまま埋め込めない

    uint16_t unitsPerEm = 1000;
    int16_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;   ///< head の FontBBox
    int16_t ascender = 0, descender = 0;              ///< hhea
    int16_t capHeight = 0;                            ///< OS/2（無ければ 0）
    float italicAngle = 0.0f;                         ///< post
    bool isFixedPitch = false;
    bool isSerif = false;                             ///< OS/2 の PANOSE から推定
};

/**
 * sfnt バイト列を解析する
 * @return 解析できたら true
 */
bool parseSfnt(const uint8_t* data, size_t size, SfntInfo& out);

} // namespace richtext::pdf

#endif // RICHTEXT_PDF_SFNT_INFO_HPP
