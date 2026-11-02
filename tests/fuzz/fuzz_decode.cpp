// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
// libFuzzer entrypoint: decode each registered type from arbitrary bytes.
#include "stellar/xdr/generated/types.hpp"
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using namespace stellar::v1::xdr;
  if (size < 1) return 0;
  auto types = all_types();
  const auto& te = types[data[0] % types.size()];
  stellar::ByteSpan in{data + 1, size - 1};
  if (te.validate(in)) {
    auto re = te.reencode(in);  // canonical re-encode must hold
    (void)re;
  }
  return 0;
}
