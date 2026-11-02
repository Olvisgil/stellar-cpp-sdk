// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
/// stellar-xdr-cpp: inspect and validate Stellar XDR values.
///
///   stellar-xdr-cpp types                          list registered type names
///   stellar-xdr-cpp decode <Type> [--hex] <data>   decode + re-encode (canonical check)
///   stellar-xdr-cpp fields <Type> [--hex] <data>   print top-level fields
///   stellar-xdr-cpp random <Type> [seed]           emit a random value as base64
///
/// <data> is base64 by default (the RPC/Horizon wire form) or hex with --hex.

#include "stellar/xdr/generated/types.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using stellar::v1::base64_decode;
using stellar::v1::base64_encode;
using stellar::v1::Bytes;
using stellar::v1::ByteSpan;
using stellar::v1::hex_decode;
using namespace stellar::v1::xdr;

namespace {

int fail(std::string_view msg) {
  std::fprintf(stderr, "error: %.*s\n", static_cast<int>(msg.size()), msg.data());
  return 1;
}

stellar::v1::Result<Bytes> read_input(int argc, char** argv, int start) {
  bool hex = false;
  std::string_view data;
  for (int i = start; i < argc; ++i) {
    if (std::strcmp(argv[i], "--hex") == 0) {
      hex = true;
    } else {
      data = argv[i];
    }
  }
  if (data.empty()) return stellar::v1::Error::invalid_argument("missing data argument");
  return hex ? hex_decode(data) : base64_decode(data);
}

int cmd_types() {
  for (const auto& te : all_types()) std::puts(te.name);
  return 0;
}

int cmd_decode(int argc, char** argv) {
  if (argc < 4) return fail("usage: decode <Type> [--hex] <data>");
  const TypeEntry* te = find_type(argv[2]);
  if (!te) return fail("unknown type (see 'types')");
  auto bytes = read_input(argc, argv, 3);
  if (!bytes) return fail(bytes.error().describe());
  if (!te->validate(*bytes)) return fail("not valid XDR for that type");
  auto re = te->reencode(*bytes);
  if (!re) return fail(re.error().describe());
  if (*re != *bytes) return fail("decodes but re-encodes non-canonically");
  std::puts(base64_encode(*bytes).c_str());
  return 0;
}

int cmd_fields(int argc, char** argv) {
  if (argc < 4) return fail("usage: fields <Type> [--hex] <data>");
  const TypeEntry* te = find_type(argv[2]);
  if (!te) return fail("unknown type (see 'types')");
  auto bytes = read_input(argc, argv, 3);
  if (!bytes) return fail(bytes.error().describe());
  if (!te->validate(*bytes)) return fail("not valid XDR for that type");
  auto r = te->visit(*bytes, nullptr, [](void*, const char* n) { std::printf("  %s\n", n); });
  if (!r) return fail(r.error().describe());
  return 0;
}

int cmd_random(int argc, char** argv) {
  if (argc < 3) return fail("usage: random <Type> [seed]");
  const TypeEntry* te = find_type(argv[2]);
  if (!te) return fail("unknown type (see 'types')");
  std::uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 0) : 1;
  auto made = te->make_random(&seed, 0);
  if (!made) return fail(made.error().describe());
  std::puts(base64_encode(*made).c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s types|decode|fields|random ...\n", argv[0]);
    return 2;
  }
  const std::string_view cmd = argv[1];
  if (cmd == "types") return cmd_types();
  if (cmd == "decode") return cmd_decode(argc, argv);
  if (cmd == "fields") return cmd_fields(argc, argv);
  if (cmd == "random") return cmd_random(argc, argv);
  return fail("unknown command");
}
