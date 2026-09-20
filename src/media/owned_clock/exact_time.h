#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace media_owned {
// Fixed storage only. Bounds are established by Clock: at most 166 numerator
// bits, 126 seconds-denominator bits, and 180 bits during binary64 rounding.
// No compiler extended integers or platform-dependent long double arithmetic.
struct Wide {
    std::array<std::uint32_t, 8> word{};
    explicit Wide(std::uint64_t n = 0) {
        word[0] = std::uint32_t(n); word[1] = std::uint32_t(n >> 32);
    }
    int compare(const Wide& b) const {
        for (unsigned i = 8; i-- > 0;) {
            if (word[i] != b.word[i]) return word[i] < b.word[i] ? -1 : 1;
        }
        return 0;
    }
    unsigned bits() const {
        for (unsigned i = 8; i-- > 0;) {
            if (word[i]) {
                unsigned n = 32 * i;
                for (auto v = word[i]; v; v >>= 1) ++n;
                return n;
            }
        }
        return 0;
    }
    Wide shifted(unsigned n) const {
        Wide out;
        const unsigned whole = n / 32, part = n % 32;
        for (unsigned i = 0; i + whole < 8; ++i) {
            out.word[i + whole] |= word[i] << part;
            if (part && i + whole + 1 < 8)
                out.word[i + whole + 1] |= word[i] >> (32 - part);
        }
        return out;
    }
    void add(const Wide& b) {
        std::uint64_t carry = 0;
        for (unsigned i = 0; i < 8; ++i) {
            const auto n = std::uint64_t(word[i]) + b.word[i] + carry;
            word[i] = std::uint32_t(n); carry = n >> 32;
        }
    }
    void subtract(const Wide& b) { // caller establishes *this >= b
        std::uint64_t borrow = 0;
        for (unsigned i = 0; i < 8; ++i) {
            const auto sub = std::uint64_t(b.word[i]) + borrow;
            borrow = std::uint64_t(word[i]) < sub;
            word[i] = std::uint32_t(std::uint64_t(word[i]) - sub);
        }
    }
    Wide times(std::uint64_t b) const {
        Wide out;
        for (unsigned j = 0; j < 2; ++j) {
            const auto digit = std::uint32_t(b >> (32 * j));
            std::uint64_t carry = 0;
            for (unsigned i = 0; i + j < 8; ++i) {
                const auto n = std::uint64_t(word[i]) * digit +
                               out.word[i + j] + carry;
                out.word[i + j] = std::uint32_t(n); carry = n >> 32;
            }
        }
        return out;
    }
};

// Round exact positive N/D once to nearest-even binary64, without intermediate
// floating divisions. Clock's finite domain never approaches subnormal/overflow.
inline double seconds_double(Wide n, const Wide& d) {
    static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53, "binary64 required");
    if (!n.bits()) return 0.0;
    int exponent = int(n.bits()) - int(d.bits());
    const int cmp = exponent >= 0 ? n.compare(d.shifted(unsigned(exponent)))
                                 : n.shifted(unsigned(-exponent)).compare(d);
    if (cmp < 0) --exponent;
    const int scale = 52 - exponent;
    if (scale >= 0) n = n.shifted(unsigned(scale));
    const Wide divisor = scale < 0 ? d.shifted(unsigned(-scale)) : d;
    std::uint64_t quotient = 0;
    for (unsigned bit = 54; bit-- > 0;) {
        const Wide shifted = divisor.shifted(bit);
        if (n.compare(shifted) >= 0) {
            n.subtract(shifted); quotient |= std::uint64_t(1) << bit;
        }
    }
    const int half = n.shifted(1).compare(divisor);
    if (half > 0 || (half == 0 && (quotient & 1))) ++quotient;
    return std::ldexp(double(quotient), exponent - 52);
}

struct Milliseconds {
    double seconds = 0;
    double scaled = 0;
    std::int32_t truncated = 0;
    bool in_range = true;
};
inline Milliseconds legacy_ms(const Wide& n, const Wide& seconds_denominator) {
    Milliseconds out;
    out.seconds = seconds_double(n, seconds_denominator);
    // Standalone contract: round-to-nearest/even FP environment, no fast-math.
    // Does not set rounding controls or preserve caller FP exception flags.
    // A future engine boundary must establish this contract and preserve state.
    // Separate binary64 store forces the original multiply's rounding boundary,
    // including on implementations that otherwise retain excess FP precision.
    volatile double rounded = out.seconds * 1000.0;
    out.scaled = rounded;
    out.in_range = out.scaled >= 0 && out.scaled < 2147483648.0;
    out.truncated = out.in_range ? std::int32_t(out.scaled) : INT32_MIN;
    return out;
}
} // namespace media_owned
