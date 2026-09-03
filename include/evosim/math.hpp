// Deterministic transcendentals.
//
// libm is NOT part of this project's reproducibility contract. Two concrete
// reasons, one of which actually bit:
//
//  1. At -O3 clang recognises an adjacent sin(x)/cos(x) pair on the same
//     argument and rewrites it into Apple libm's __sincos_stret. That routine
//     returns a sine that differs from sin() by one ULP on some inputs, so a
//     release build and a debug build produced different initial velocities
//     for 4 agents out of 1000 -- and therefore different state hashes from
//     tick zero. The compile flags cannot prevent this; the call itself has to
//     go.
//  2. sin, cos and log are not correctly rounded and are not specified to
//     agree between platforms, so a libm-based state hash could never be
//     compared across machines.
//
// These are ordinary polynomial evaluations: a fixed sequence of IEEE-754
// multiplies and adds. Without fast-math (which would license reassociation)
// and with -ffp-contract=off (which would license FMA fusion), the compiler is
// required to produce the same result at every optimisation level.
//
// sqrt is left to the hardware on purpose: IEEE-754 requires it to be
// correctly rounded, so it is already exact and identical everywhere.
#pragma once

#include <cmath>
#include <cstdint>

namespace evosim::mathd {

inline constexpr double kPi     = 3.14159265358979311599796346854;
inline constexpr double kTwoPi  = 6.28318530717958623199592693709;
inline constexpr double kHalfPi = 1.57079632679489655799898173427;

namespace detail {

// pi/2 split so that k * PIO2_x is exact for the small |k| this simulation
// produces (arguments stay within a few multiples of 2*pi).
inline constexpr double kPio2Hi = 1.57079632673412561417e+00;
inline constexpr double kPio2Mid = 6.07710050650619224932e-11;
inline constexpr double kPio2Lo = 2.02226624879595063154e-21;

// fdlibm kernel coefficients (Sun Microsystems, public domain).
inline constexpr double S1 = -1.66666666666666324348e-01;
inline constexpr double S2 =  8.33333333332248946124e-03;
inline constexpr double S3 = -1.98412698298579493134e-04;
inline constexpr double S4 =  2.75573137070700676789e-06;
inline constexpr double S5 = -2.50507602534068634195e-08;
inline constexpr double S6 =  1.58969099521155010221e-10;

inline constexpr double C1 =  4.16666666666666019037e-02;
inline constexpr double C2 = -1.38888888888741095749e-03;
inline constexpr double C3 =  2.48015872894767294178e-05;
inline constexpr double C4 = -2.75573143513906633035e-07;
inline constexpr double C5 =  2.08757232129817482790e-09;
inline constexpr double C6 = -1.13596475577881948265e-11;

// sin(r) for |r| <= pi/4.
inline double kernel_sin(double r) noexcept {
    const double z = r * r;
    const double p = S1 + z * (S2 + z * (S3 + z * (S4 + z * (S5 + z * S6))));
    return r + r * z * p;
}

// cos(r) for |r| <= pi/4. The 1 - z/2 head is kept separate so the polynomial
// only has to carry the small remainder.
inline double kernel_cos(double r) noexcept {
    const double z = r * r;
    const double p = C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6))));
    return 1.0 - 0.5 * z + z * z * p;
}

// Cody-Waite reduction: x = k*(pi/2) + r with |r| <= pi/4. floor() is exact and
// independent of the current rounding mode, unlike nearbyint().
inline int64_t reduce(double x, double& r) noexcept {
    const double kd = std::floor(x * (1.0 / kHalfPi) + 0.5);
    r = ((x - kd * kPio2Hi) - kd * kPio2Mid) - kd * kPio2Lo;
    return static_cast<int64_t>(kd);
}

}  // namespace detail

inline double dsin(double x) noexcept {
    double r;
    const int64_t q = detail::reduce(x, r);
    switch (q & 3) {
        case 0:  return  detail::kernel_sin(r);
        case 1:  return  detail::kernel_cos(r);
        case 2:  return -detail::kernel_sin(r);
        default: return -detail::kernel_cos(r);
    }
}

inline double dcos(double x) noexcept {
    double r;
    const int64_t q = detail::reduce(x, r);
    switch (q & 3) {
        case 0:  return  detail::kernel_cos(r);
        case 1:  return -detail::kernel_sin(r);
        case 2:  return -detail::kernel_cos(r);
        default: return  detail::kernel_sin(r);
    }
}

// Natural log for x > 0. frexp and the power-of-two scaling below are exact, so
// only the atanh series contributes rounding.
inline double dlog(double x) noexcept {
    if (!(x > 0.0)) return x == 0.0 ? -INFINITY : NAN;

    int e = 0;
    double m = std::frexp(x, &e);          // x = m * 2^e, m in [0.5, 1)
    if (m < 0.70710678118654752440) {      // centre on 1 so the series converges fast
        m *= 2.0;                          // exact: power of two
        --e;
    }

    // log(m) = 2 * atanh(s), s = (m-1)/(m+1), |s| <= 0.1716
    const double s  = (m - 1.0) / (m + 1.0);
    const double z  = s * s;
    const double series =
        1.0 / 3.0 + z * (1.0 / 5.0 + z * (1.0 / 7.0 + z * (1.0 / 9.0 + z * (1.0 / 11.0 +
        z * (1.0 / 13.0 + z * (1.0 / 15.0 + z * (1.0 / 17.0 + z * (1.0 / 19.0 +
        z * (1.0 / 21.0)))))))));

    constexpr double kLn2Hi = 6.93147180369123816490e-01;
    constexpr double kLn2Lo = 1.90821492927058770002e-10;
    const double ed = static_cast<double>(e);
    return ed * kLn2Hi + (2.0 * (s + s * z * series) + ed * kLn2Lo);
}

}  // namespace evosim::mathd
