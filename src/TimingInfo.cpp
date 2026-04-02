#include "richtext/TimingInfo.hpp"

#include <algorithm>

namespace richtext {

/**
 * 1区間分のタイミングを解決する内部ヘルパー
 *
 * entries[rangeStart..rangeEnd) を diff/all に基づいて処理し、
 * result / outKeyWaits に追加する。
 * currentTime は区間をまたいで引き継がれる。
 */
static void resolveSection(
    const std::vector<TimingEntry>& entries,
    size_t rangeStart, size_t rangeEnd,
    float diff, float all,
    const std::vector<float>* charWidths,
    float& currentTime,
    std::vector<ResolvedTiming>& result,
    std::vector<KeyWaitInfo>* outKeyWaits)
{
    // diff == 0 の場合、delay/wait/sync を無視（瞬間表示）
    bool instantMode = (diff == 0.0f);

    // 文字幅の平均値（charWidths 用の基準）
    float avgWidth = 0.0f;
    if (charWidths && !charWidths->empty()) {
        float sum = 0.0f;
        int count = 0;
        for (float w : *charWidths) {
            if (w > 0.0f) {
                sum += w;
                ++count;
            }
        }
        avgWidth = (count > 0) ? (sum / count) : 1.0f;
    }

    // この区間の開始インデックス（all スケーリング用）
    size_t sectionResultStart = result.size();
    size_t sectionKeyWaitStart = outKeyWaits ? outKeyWaits->size() : 0;
    float sectionStartTime = currentTime;

    for (size_t i = rangeStart; i < rangeEnd; ++i) {
        const auto& entry = entries[i];

        switch (entry.type) {
        case TimingEntry::Type::Start:
            // 区間内では出現しない（外側で分割済み）
            break;

        case TimingEntry::Type::Sync: {
            if (instantMode) break;
            if (entry.syncMs >= 0.0f) {
                currentTime = entry.syncMs;
            }
            break;
        }

        case TimingEntry::Type::Wait: {
            if (instantMode) break;
            if (entry.waitMs >= 0.0f) {
                currentTime += entry.waitMs;
            } else if (entry.waitPercent >= 0.0f) {
                currentTime += diff * (entry.waitPercent / 100.0f);
            }
            break;
        }

        case TimingEntry::Type::KeyWait: {
            if (instantMode) break;
            if (outKeyWaits) {
                outKeyWaits->push_back({entry.charIndex, currentTime});
            }
            break;
        }

        case TimingEntry::Type::Char: {
            ResolvedTiming rt;
            rt.delay = currentTime;
            rt.charIndex = entry.charIndex;
            result.push_back(rt);

            if (!instantMode) {
                float charDelay;
                if (entry.delayMs >= 0.0f) {
                    charDelay = entry.delayMs;
                } else {
                    charDelay = diff * (entry.delayPercent / 100.0f);
                }

                if (charWidths && avgWidth > 0.0f && entry.charIndex >= 0
                    && entry.charIndex < static_cast<int>(charWidths->size())) {
                    float w = (*charWidths)[entry.charIndex];
                    if (w > 0.0f) {
                        charDelay *= (w / avgWidth);
                    }
                }

                currentTime += charDelay;
            }
            break;
        }
        }
    }

    // all 指定による区間内スケーリング
    if (all > 0.0f) {
        float sectionDuration = currentTime - sectionStartTime;
        if (sectionDuration > 0.0f) {
            float scale = all / sectionDuration;
            for (size_t j = sectionResultStart; j < result.size(); ++j) {
                result[j].delay = sectionStartTime + (result[j].delay - sectionStartTime) * scale;
            }
            if (outKeyWaits) {
                for (size_t j = sectionKeyWaitStart; j < outKeyWaits->size(); ++j) {
                    (*outKeyWaits)[j].delay = sectionStartTime + ((*outKeyWaits)[j].delay - sectionStartTime) * scale;
                }
            }
            currentTime = sectionStartTime + all;
        }
    }
}

std::vector<ResolvedTiming> resolveTimings(
    const std::vector<TimingEntry>& entries,
    float timeScale,
    const std::vector<float>* charWidths,
    std::vector<KeyWaitInfo>* outKeyWaits)
{
    std::vector<ResolvedTiming> result;
    result.reserve(entries.size());

    if (outKeyWaits) {
        outKeyWaits->clear();
    }

    float currentTime = 0.0f;

    // Start エントリで区間を分割して処理
    // 冒頭に Start がない場合の初期値は diff=0, all=0
    float currentDiff = 0.0f;
    float currentAll = 0.0f;
    size_t sectionStart = 0;

    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].type == TimingEntry::Type::Start) {
            // 手前の区間を処理
            if (i > sectionStart) {
                resolveSection(entries, sectionStart, i,
                              currentDiff, currentAll,
                              charWidths,
                              currentTime, result, outKeyWaits);
            }
            // 新しい区間の diff/all を設定
            currentDiff = entries[i].startDiff;
            currentAll = entries[i].startAll;
            sectionStart = i + 1;
        }
    }

    // 最後の区間を処理
    if (sectionStart < entries.size()) {
        resolveSection(entries, sectionStart, entries.size(),
                      currentDiff, currentAll,
                      charWidths,
                      currentTime, result, outKeyWaits);
    }

    // timeScale 適用
    if (timeScale != 1.0f && timeScale > 0.0f) {
        for (auto& rt : result) {
            rt.delay *= timeScale;
        }
        if (outKeyWaits) {
            for (auto& kw : *outKeyWaits) {
                kw.delay *= timeScale;
            }
        }
    }

    return result;
}

} // namespace richtext
