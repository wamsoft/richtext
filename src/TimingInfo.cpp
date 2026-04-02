#include "richtext/TimingInfo.hpp"

#include <algorithm>
#include <numeric>
#include <cmath>

namespace richtext {

std::vector<ResolvedTiming> resolveTimings(
    const std::vector<TimingEntry>& entries,
    float diff,
    float all,
    float timeScale,
    bool widthTimeScale,
    const std::vector<float>& charWidths,
    const LabelResolver& labelResolver,
    std::vector<KeyWaitInfo>* outKeyWaits)
{
    std::vector<ResolvedTiming> result;
    result.reserve(entries.size());

    if (outKeyWaits) {
        outKeyWaits->clear();
    }

    // 文字幅の平均値（widthTimeScale 用の基準）
    float avgWidth = 0.0f;
    if (widthTimeScale && !charWidths.empty()) {
        float sum = 0.0f;
        int count = 0;
        for (float w : charWidths) {
            if (w > 0.0f) {
                sum += w;
                ++count;
            }
        }
        avgWidth = (count > 0) ? (sum / count) : 1.0f;
    }

    float currentTime = 0.0f;

    for (const auto& entry : entries) {
        switch (entry.type) {
        case TimingEntry::Type::Sync: {
            // 絶対時間同期
            if (!entry.syncLabel.empty() && labelResolver) {
                currentTime = labelResolver(entry.syncLabel);
            } else if (entry.syncMs >= 0.0f) {
                currentTime = entry.syncMs;
            }
            break;
        }

        case TimingEntry::Type::Wait: {
            // 時間待ち（文字なし）
            if (entry.waitMs >= 0.0f) {
                currentTime += entry.waitMs;
            } else if (entry.waitPercent >= 0.0f) {
                currentTime += diff * (entry.waitPercent / 100.0f);
            }
            break;
        }

        case TimingEntry::Type::KeyWait: {
            if (outKeyWaits) {
                outKeyWaits->push_back({entry.charIndex, currentTime});
            }
            break;
        }

        case TimingEntry::Type::Char: {
            // 表示開始時間を記録
            ResolvedTiming rt;
            rt.delay = currentTime;
            rt.charIndex = entry.charIndex;
            result.push_back(rt);

            // この文字の表示時間を計算して累積
            float charDelay;
            if (entry.delayMs >= 0.0f) {
                // 絶対時間指定
                charDelay = entry.delayMs;
            } else {
                // パーセント指定
                charDelay = diff * (entry.delayPercent / 100.0f);
            }

            // 文字幅による補正
            if (widthTimeScale && avgWidth > 0.0f && entry.charIndex >= 0
                && entry.charIndex < static_cast<int>(charWidths.size())) {
                float w = charWidths[entry.charIndex];
                if (w > 0.0f) {
                    charDelay *= (w / avgWidth);
                }
            }

            currentTime += charDelay;
            break;
        }
        }
    }

    // all 指定による全体スケーリング
    if (all > 0.0f && currentTime > 0.0f) {
        float scale = all / currentTime;
        for (auto& rt : result) {
            rt.delay *= scale;
        }
        if (outKeyWaits) {
            for (auto& kw : *outKeyWaits) {
                kw.delay *= scale;
            }
        }
        currentTime = all;
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
