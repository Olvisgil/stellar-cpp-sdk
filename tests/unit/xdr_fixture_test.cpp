// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0
// Known-answer fixtures captured from the live network by tools/fetch_fixtures.py.
// Every fixture must decode strictly and re-encode byte-identically.
#include "stellar/xdr/generated/types.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace stellar::v1;
using namespace stellar::v1::xdr;

static Result<Bytes> read_b64(const std::string& path) {
  std::ifstream f(path);
  if (!f) return stellar::v1::Error::invalid_argument("cannot open fixture " + path);
  std::string line;
  std::getline(f, line);
  return base64_decode(line);
}

TEST(XdrFixtures, Manifest) {
  const std::string dir = STELLAR_FIXTURES_DIR;
  std::ifstream mf(dir + "/manifest.tsv");
  ASSERT_TRUE(mf.good());
  std::string line;
  int count = 0;
  while (std::getline(mf, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string file, type, note;
    std::getline(ss, file, '\t');
    std::getline(ss, type, '\t');
    std::getline(ss, note, '\t');
    const auto* te = find_type(type);
    ASSERT_NE(te, nullptr) << type;
    auto bytes = read_b64(dir + "/" + file);
    ASSERT_TRUE(bytes.ok()) << file;
    ASSERT_TRUE(te->validate(*bytes)) << file << " (" << note << ")";
    auto re = te->reencode(*bytes);
    ASSERT_TRUE(re.ok()) << file;
    EXPECT_EQ(*re, *bytes) << file << " re-encodes non-canonically";
    ++count;
  }
  EXPECT_GE(count, 10) << "fixtures missing? run tools/fetch_fixtures.py";
}
