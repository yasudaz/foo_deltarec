// ntf.hpp - Noise Transfer Function designer for 1-bit delta-sigma modulators.
//
// Produces a monic rational NTF(z) = N(z)/D(z) with
//   * zeros spread across the signal band at Gauss-Legendre optimised
//     positions (minimises in-band noise power), and
//   * Butterworth-type poles whose radius is bisected until the
//     out-of-band gain ||NTF||_inf equals the requested H_inf (Lee's rule).
//
// This is the classic textbook recipe (Schreier & Temes, "Understanding
// Delta-Sigma Data Converters", ch. 4/8).  Nothing here depends on the
// MATLAB Delta-Sigma Toolbox; it is re-derived from first principles.
#pragma once
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace deltarec {

constexpr double kPi = 3.14159265358979323846;

struct NtfDesign {
    std::vector<double> b;  // numerator   (b[0] == 1)
    std::vector<double> a;  // denominator (a[0] == 1)
    int order = 0;
    double osr = 0;
    double hinf = 0;
};

namespace detail {

// Normalised optimal zero positions (fraction of the band edge).
// These are the Gauss-Legendre nodes of the given order, which minimise
// the integral of |prod (x - z_i)|^2 over [-1, 1].
inline std::vector<double> optimalZeros(int order) {
    switch (order) {
    case 1: return {0.0};
    case 2: return {0.57735027, -0.57735027};
    case 3: return {0.0, 0.77459667, -0.77459667};
    case 4: return {0.33998104, 0.86113631, -0.33998104, -0.86113631};
    case 5: return {0.0, 0.53846931, 0.90617985, -0.53846931, -0.90617985};
    case 6: return {0.23861919, 0.66120939, 0.93246951, -0.23861919, -0.66120939, -0.93246951};
    case 7: return {0.0, 0.40584515, 0.74153119, 0.94910791, -0.40584515, -0.74153119, -0.94910791};
    case 8: return {0.18343464, 0.52553241, 0.79666648, 0.96028986, -0.18343464, -0.52553241, -0.79666648, -0.96028986};
    default: throw std::runtime_error("NTF order must be 1..8");
    }
}

using cd = std::complex<double>;

// Expand prod (z - r_i) into polynomial coefficients (descending powers).
inline std::vector<cd> polyFromRoots(const std::vector<cd>& roots) {
    std::vector<cd> p{1.0};
    for (const cd& r : roots) {
        std::vector<cd> q(p.size() + 1, 0.0);
        for (size_t i = 0; i < p.size(); ++i) {
            q[i] += p[i];
            q[i + 1] -= p[i] * r;
        }
        p.swap(q);
    }
    return p;
}

// Analog Butterworth poles (unit cutoff), scaled by wc, then bilinear
// transformed to z (fs = 2 convention, i.e. z = (1 + s/2) / (1 - s/2)).
inline std::vector<cd> butterworthPolesZ(int order, double wc) {
    std::vector<cd> out;
    for (int k = 1; k <= order; ++k) {
        double theta = kPi * (2.0 * k + order - 1.0) / (2.0 * order);
        cd s = wc * cd(std::cos(theta), std::sin(theta));
        out.push_back((1.0 + s / 2.0) / (1.0 - s / 2.0));
    }
    return out;
}

inline double maxGainOnUnitCircle(const std::vector<cd>& zeros, const std::vector<cd>& poles) {
    const int N = 8192;
    double best = 0.0;
    for (int i = 0; i <= N; ++i) {
        double w = kPi * i / N;
        cd z = std::polar(1.0, w);
        cd num = 1.0, den = 1.0;
        for (const cd& q : zeros) num *= (z - q);
        for (const cd& p : poles) den *= (z - p);
        double g = std::abs(num / den);
        if (g > best) best = g;
    }
    return best;
}

}  // namespace detail

// Design an NTF.  osr = fs_dsd / (2 * band_edge).
inline NtfDesign designNtf(int order, double osr, double hinf) {
    using namespace detail;
    if (hinf <= 1.0) throw std::runtime_error("H_inf must be > 1");
    NtfDesign d;
    d.order = order;
    d.osr = osr;
    d.hinf = hinf;

    double wb = kPi / osr;
    std::vector<cd> zeros;
    for (double zn : optimalZeros(order)) zeros.push_back(std::polar(1.0, zn * wb));

    // Bisect the Butterworth cutoff so that ||NTF||_inf == hinf.
    double lo = 0.005, hi = 3.0;
    for (int it = 0; it < 100; ++it) {
        double mid = 0.5 * (lo + hi);
        if (maxGainOnUnitCircle(zeros, butterworthPolesZ(order, mid)) > hinf) hi = mid;
        else lo = mid;
    }
    std::vector<cd> poles = butterworthPolesZ(order, lo);

    auto num = polyFromRoots(zeros);
    auto den = polyFromRoots(poles);
    d.b.resize(num.size());
    d.a.resize(den.size());
    for (size_t i = 0; i < num.size(); ++i) d.b[i] = num[i].real();
    for (size_t i = 0; i < den.size(); ++i) d.a[i] = den[i].real();
    // Zeros come in conjugate pairs (plus z=1 for odd order) so the
    // polynomial is real; force exact symmetry to kill rounding dust.
    d.b[0] = 1.0;
    d.a[0] = 1.0;
    return d;
}

// Theoretical in-band SNR (dB) for a sine of amplitude `amp` under the
// white-noise model of the 1-bit quantiser (error variance = 1/3).
inline double theoreticalSnrDb(const NtfDesign& d, double amp = 0.5) {
    const int N = 4000;
    double wb = kPi / d.osr;
    double acc = 0.0;
    for (int i = 0; i <= N; ++i) {
        double w = wb * i / N;
        detail::cd z = std::polar(1.0, w);
        detail::cd num = 0.0, den = 0.0;
        for (double c : d.b) num = num * z + c;
        for (double c : d.a) den = den * z + c;
        double g2 = std::norm(num / den);
        acc += (i == 0 || i == N) ? 0.5 * g2 : g2;
    }
    double noisePower = (1.0 / 3.0) * (acc * (wb / N)) / kPi;
    double sigPower = amp * amp / 2.0;
    return 10.0 * std::log10(sigPower / noisePower);
}

}  // namespace deltarec
