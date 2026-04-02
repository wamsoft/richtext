#ifndef RICHTEXT_TIMING_INFO_HPP
#define RICHTEXT_TIMING_INFO_HPP

#include <vector>

namespace richtext {

/**
 * タイミングエントリ（文字表示タイミングの元データ）
 *
 * タグパース時に生成され、resolveTimings() で
 * 実際の表示時間に変換される。
 */
struct TimingEntry {
    enum class Type {
        Char,       ///< 通常文字（表示遅延を持つ）
        Wait,       ///< 時間待ち（文字なし、待ち時間のみ）
        Sync,       ///< 時間同期（絶対時間）
        KeyWait,    ///< キー入力待ち
        Start,      ///< 区間開始（diff/all を定義）
    };

    Type type = Type::Char;

    int charIndex = -1;             ///< 対応する文字インデックス（plainText内）

    // 文字表示時間パラメータ（Type::Char 用）
    float delayPercent = 100.0f;    ///< %d: 標準時間に対するパーセント
    float delayMs = -1.0f;          ///< %a: 絶対時間指定（ms）、-1 = 未指定

    // 待ち時間パラメータ（Type::Wait 用）
    float waitPercent = -1.0f;      ///< %w: 1文字時間に対するパーセント
    float waitMs = -1.0f;           ///< %t: 絶対待ち時間（ms）

    // 同期パラメータ（Type::Sync 用）
    float syncMs = -1.0f;           ///< %D: 絶対同期時間（ms）

    // 区間パラメータ（Type::Start 用）
    float startDiff = 0.0f;         ///< 1文字あたり基準表示時間（ms）
    float startAll = 0.0f;          ///< 全体表示時間（ms、0 = 自動）
};

/**
 * 解決済みタイミング（各文字の実際の表示開始時間）
 */
struct ResolvedTiming {
    float delay;        ///< 表示開始時間（ms）
    int charIndex;      ///< 対応する文字インデックス
};

/**
 * キー入力待ち情報
 */
struct KeyWaitInfo {
    int charIndex;      ///< 文字位置
    float delay;        ///< その時点での経過時間（ms）
};

/**
 * タイミング情報を解決し、各文字の表示開始時間を計算する
 *
 * Type::Start エントリで区間を区切り、各区間の diff/all に基づいて計算する。
 * 冒頭に Start がない場合の初期値は diff=0, all=0（瞬間表示）。
 *
 * @param entries       TimingEntry 配列
 * @param timeScale     時間スケール係数
 * @param charWidths    各文字の幅配列（nullptr の場合は幅補正なし）
 * @param outKeyWaits   キー待ち情報の出力先（nullptr 可）
 * @return 各文字の表示開始時間配列
 */
std::vector<ResolvedTiming> resolveTimings(
    const std::vector<TimingEntry>& entries,
    float timeScale,
    const std::vector<float>* charWidths = nullptr,
    std::vector<KeyWaitInfo>* outKeyWaits = nullptr
);

} // namespace richtext

#endif // RICHTEXT_TIMING_INFO_HPP
