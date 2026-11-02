// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
// Per-type libFuzzer target: built once per registered type name via
// -DFUZZ_XDR_TYPE=<TypeName>. Decodes strictly, then requires that a
// successful decode re-encodes to identical bytes.
#include "stellar/xdr/generated/types.hpp"

#ifndef FUZZ_XDR_TYPE
#error "define FUZZ_XDR_TYPE=<TypeName>"
#endif

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using namespace stellar::v1::xdr;
  ByteSpan in{data, size};
  auto dec = decode<FUZZ_XDR_TYPE>(in);
  if (dec.ok()) {
    auto re = encode(*dec);
    if (!re.ok() || re->size() != in.size() || !std::equal(re->begin(), re->end(), in.begin())) {
      __builtin_trap();  // canonical re-encode violated
    }
  }
  return 0;
}
