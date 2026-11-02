// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/price.hpp"

#include <cctype>
#include <numeric>

namespace stellar::inline v1 {

Price::Price(std::int32_t n, std::int32_t d) : n_(n), d_(d) {}

Result<Price> Price::best_rational(std::int64_t num, std::int64_t den) {
  if (den <= 0 || num < 0) {
    return Error::invalid_argument("price must be non-negative with positive denominator");
  }
  if (num <= INT32_MAX && den <= INT32_MAX) {
    return Price(static_cast<std::int32_t>(num), static_cast<std::int32_t>(den));
  }
  // Stern-Brocot search for the best approximation with n,d <= INT32_MAX.
  const long double target = static_cast<long double>(num) / static_cast<long double>(den);
  std::int64_t ln = 0, ld = 1;  // lower bound 0/1
  std::int64_t hn = 1, hd = 0;  // upper bound 1/0 (inf)
  Price best{static_cast<std::int32_t>(num < den ? 0 : INT32_MAX),
             static_cast<std::int32_t>(num < den ? 1 : 1)};
  long double best_err = 1e30L;
  for (int iter = 0; iter < 128; ++iter) {
    const std::int64_t mn = ln + hn;
    const std::int64_t md = ld + hd;
    if (mn > INT32_MAX || md > INT32_MAX) break;
    const long double m = static_cast<long double>(mn) / static_cast<long double>(md);
    const long double err = m > target ? m - target : target - m;
    if (err < best_err) {
      best_err = err;
      best = Price(static_cast<std::int32_t>(mn), static_cast<std::int32_t>(md));
    }
    if (err == 0) break;
    if (m < target) {
      ln = mn;
      ld = md;
    } else {
      hn = mn;
      hd = md;
    }
  }
  return best;
}

Result<Price> Price::parse(std::string_view text) {
  const auto slash = text.find('/');
  if (slash != std::string_view::npos) {
    // "n/d"
    auto parse_int = [](std::string_view s, std::int64_t* out) -> bool {
      if (s.empty()) return false;
      std::int64_t v = 0;
      for (char c : s) {
        if (c < '0' || c > '9') return false;
        if (v > (INT64_MAX - (c - '0')) / 10) return false;
        v = v * 10 + (c - '0');
      }
      *out = v;
      return true;
    };
    std::int64_t n = 0, d = 0;
    if (!parse_int(text.substr(0, slash), &n) || !parse_int(text.substr(slash + 1), &d) || d == 0 || n < 0) {
      return Error::invalid_argument("price: expected 'N/D' with positive D");
    }
    // reduce
    const std::int64_t g = std::gcd(n, d);
    return best_rational(n / g, d / g);
  }
  // decimal -> rational with denominator 10^k
  std::string s(text);
  auto dot = s.find('.');
  std::int64_t den = 1;
  if (dot != std::string::npos) {
    const std::size_t frac_len = s.size() - dot - 1;
    if (frac_len == 0 || frac_len > 18) {
      return Error::invalid_argument("price: too many decimal places");
    }
    s.erase(dot, 1);
    for (std::size_t i = 0; i < frac_len; ++i) den *= 10;
  }
  if (s.empty()) return Error::invalid_argument("price: empty");
  std::int64_t num = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return Error::invalid_argument("price: non-digit");
    if (num > (INT64_MAX - (c - '0')) / 10) return Error{ErrorCode::kOutOfRange, "price overflows"};
    num = num * 10 + (c - '0');
  }
  const std::int64_t g = std::gcd(num, den);
  return best_rational(num / g, den / g);
}

std::string Price::to_string() const {
  const std::int64_t g = std::gcd(static_cast<std::int64_t>(n_), static_cast<std::int64_t>(d_));
  return std::to_string(n_ / g) + "/" + std::to_string(d_ / g);
}

}  // namespace stellar::inline v1
