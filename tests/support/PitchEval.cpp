// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/PitchEval.h"

#include <algorithm>
#include <cmath>

namespace subgen::test
{
double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

PitchScore evaluatePitch(const PitchTrack& track, const PitchTruth& truth, double onset)
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> t, f, conf;
    for (size_t i = 0; i < track.t.size(); ++i)
        if (track.t[i] >= onset - 1e-12)
        {
            t.push_back(track.t[i]);
            f.push_back(track.f0[i]);
            conf.push_back(track.conf[i]);
        }
    PitchScore s;
    if (t.empty())
        return s;
    auto errorCents = [&](double lag) {
        std::vector<double> e(t.size(), nan);
        for (size_t i = 0; i < t.size(); ++i)
        {
            const double tr = truth(std::max(t[i] - lag, onset));
            if (std::isfinite(tr) && tr > 0.0 && std::isfinite(f[i]) && f[i] > 0.0)
                e[i] = 1200.0 * std::log2(f[i] / tr);
        }
        return e;
    };
    const double h = 0.005;
    std::vector<bool> moving(t.size());
    int movingCount = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        const double rate = std::abs(1200.0 * std::log2(truth(t[i] + h) / truth(t[i] - h))) / (2.0 * h);
        moving[i] = std::isfinite(rate) && rate >= 500.0;
        movingCount += moving[i] ? 1 : 0;
    }
    if (movingCount >= 5)
    {
        double best = std::numeric_limits<double>::infinity();
        for (int k = 0; k <= 500; ++k)
        {
            const double tau = k * 0.0005;
            const auto e = errorCents(tau);
            double sum = 0.0;
            int count = 0;
            for (size_t i = 0; i < e.size(); ++i)
                if (moving[i] && std::isfinite(e[i]))
                {
                    sum += std::abs(e[i]);
                    ++count;
                }
            if (count >= 5 && sum / count < best - 1e-9)
            {
                best = sum / count;
                s.lag = tau;
            }
        }
    }
    const auto e = errorCents(s.lag);
    auto good = [&](size_t i) { return std::isfinite(e[i]) && std::abs(e[i]) <= 50.0; };
    size_t start = 0;
    bool locked = false;
    for (size_t i = 0; i + 3 <= e.size() && !locked; ++i)
        if (good(i) && good(i + 1) && good(i + 2))
        {
            start = i;
            locked = true;
        }
    if (locked)
        s.lockTime = t[start] - onset;
    std::vector<double> post;
    for (size_t i = start; i < e.size(); ++i)
        if (std::isfinite(e[i]))
            post.push_back(std::abs(e[i]));
    if (!post.empty())
        s.centsMedian = median(post);
    for (size_t i = 0; i < e.size(); ++i)
        s.octaveErrors += std::isfinite(e[i]) && conf[i] >= 0.5 && std::abs(e[i]) > 600.0 ? 1 : 0;
    for (size_t i = 0; i < conf.size(); ++i)
        if (conf[i] >= 0.5)
        {
            s.confOpen = t[i] - onset;
            break;
        }
    return s;
}
} // namespace subgen::test
