// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/util/clock.hpp"

#include <sodium.h>
#include <cstdio>
#include <regex>

namespace stellar::inline v1 {

void StderrLogger::log(LogLevel level, std::string_view message) {
  static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
  std::fprintf(stderr, "[%s] %s\n", names[static_cast<int>(level)], std::string(message).c_str());
}

std::string redact(std::string_view message) {
  std::string out{message};
  // strip likely secrets: 40+ char hex, S[0-9A-Z]{55} seeds, JWTs, bearer tokens
  static const std::regex patterns[] = {
      std::regex{"S[A-Z0-9]{55}"},  // secret seeds
      std::regex{
          "eyJ[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+|eyJ[A-Za-z0-9_-]{8,}"},  // JWTs
      std::regex{"[Aa][Uu][Tt][Hh][Oo][Rr][Ii][Zz][Aa][Tt][Ii][Oo][Nn]:\\s+\\S+|[Bb][Ee][Aa][Rr]["
                 "Ee][Rr]\\s+\\S+"},
      std::regex{"[0-9a-fA-F]{40,}"},  // long hex (keys/seeds)
  };
  for (const auto& p : patterns) {
    out = std::regex_replace(out, p, "<REDACTED>");
  }
  return out;
}

Bytes SecureRandom::bytes(std::size_t n) {
  Bytes out(n);
  randombytes_buf(out.data(), n);
  return out;
}

}  // namespace stellar::inline v1
