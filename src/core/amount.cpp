// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/amount.hpp"

#include <cctype>
#include <cstdio>

namespace stellar::inline v1 {

Result<Amount> Amount::parse(std::string_view text) {
  if (text.empty()) return Error::invalid_argument("amount: empty string");
  bool neg = false;
  std::size_t i = 0;
  if (text[0] == '-') {
    neg = true;
    i = 1;
    if (i == text.size()) return Error::invalid_argument("amount: lone '-'");
  } else if (text[0] == '+') {
    i = 1;
  }
  std::int64_t whole = 0;
  bool got_digit = false;
  for (; i < text.size() && text[i] != '.'; ++i) {
    const char c = text[i];
    if (c < '0' || c > '9') return Error::invalid_argument("amount: non-digit");
    got_digit = true;
    if (whole > (INT64_MAX - (c - '0')) / 10) {
      return Error{ErrorCode::kOutOfRange, "amount overflows int64"};
    }
    whole = whole * 10 + (c - '0');
  }
  std::int64_t frac = 0;
  int frac_digits = 0;
  if (i < text.size() && text[i] == '.') {
    ++i;
    for (; i < text.size(); ++i) {
      const char c = text[i];
      if (c < '0' || c > '9') return Error::invalid_argument("amount: non-digit after '.'");
      if (frac_digits >= 7) {
        if (c != '0') {
          return Error::invalid_argument("amount: more than 7 decimal places");
        }
      } else {
        frac = frac * 10 + (c - '0');
        ++frac_digits;
      }
    }
    if (frac_digits == 0) return Error::invalid_argument("amount: '.' with no digits");
  }
  if (!got_digit && frac_digits == 0) return Error::invalid_argument("amount: no digits");
  for (int d = frac_digits; d < 7; ++d) frac *= 10;
  if (whole > (INT64_MAX - frac) / kOne) {
    return Error{ErrorCode::kOutOfRange, "amount overflows int64 stroops"};
  }
  const std::int64_t stroops = whole * kOne + frac;
  return Amount(neg ? -stroops : stroops);
}

std::string Amount::to_string() const {
  const std::int64_t s = stroops_;
  const std::int64_t whole = s / kOne;
  std::int64_t frac = s % kOne;
  if (frac < 0) frac = -frac;
  if (frac == 0) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(whole));
    return buf;
  }
  // strip trailing zeros but keep at least one decimal digit
  int digits = 7;
  while (digits > 1 && frac % 10 == 0) {
    frac /= 10;
    --digits;
  }
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%lld.%0*lld", static_cast<long long>(whole), digits,
                static_cast<long long>(frac));
  return buf;
}

}  // namespace stellar::inline v1
