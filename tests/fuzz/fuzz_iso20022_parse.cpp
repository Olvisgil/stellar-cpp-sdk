// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

/// libFuzzer target: arbitrary bytes → iso20022::parse. Must never crash or
/// accept undefined behaviour; malformed input just returns an Error.

#include "stellar/iso20022/xml.hpp"

#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view sv(reinterpret_cast<const char*>(data), size);
  auto kind = stellar::iso20022::detect_message_type(sv);
  auto doc = stellar::iso20022::parse(sv);
  if (doc.ok()) {
    (void)stellar::iso20022::to_xml(*doc);
    (void)stellar::iso20022::status_reports(*doc);
  }
  (void)kind;
  return 0;
}
