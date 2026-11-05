// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace subgen::routing
{
// rho = <ab> / sqrt(<a^2><b^2>) with one-pole averages of 300 ms and a floor of 1e-12 under the division (SPEC §8.A).
class Correlation
{
public:
    void prepare(double fs)
    {
        coeff = std::exp(-1.0 / (0.3 * fs));
        reset();
    }

    void reset() { ab = aa = bb = rho = 0.0; }

    double process(double a, double b)
    {
        ab = coeff * ab + (1.0 - coeff) * a * b;
        aa = coeff * aa + (1.0 - coeff) * a * a;
        bb = coeff * bb + (1.0 - coeff) * b * b;
        rho = ab / std::max(std::sqrt(aa * bb), 1e-12);
        return rho;
    }

    double current() const { return rho; }

private:
    double coeff = 0.0;
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    double rho = 0.0;
};
} // namespace subgen::routing
