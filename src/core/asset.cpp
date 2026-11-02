// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/asset.hpp"

#include "stellar/core/keypair.hpp"  // sha256
#include "stellar/core/network.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace stellar::inline v1 {

namespace {

Result<xdr::AccountID> issuer_to_account(std::string_view issuer) {
  STELLAR_TRY_ASSIGN(auto sk, StrKey::decode(issuer));
  if (sk.kind() != StrKeyKind::kAccount) {
    return Error{ErrorCode::kInvalidAddress, "asset issuer must be a G... account"};
  }
  return xdr::PublicKey::publicKeyTypeEd25519(xdr::uint256{*sk.fixed()});
}

Result<xdr::AlphaNum4> make_num4(std::string_view code, std::string_view issuer) {
  STELLAR_TRY_ASSIGN(auto acc, issuer_to_account(issuer));
  xdr::AssetCode4 c{};
  std::memcpy(c.data(), code.data(), code.size());
  return xdr::AlphaNum4{c, acc};
}
Result<xdr::AlphaNum12> make_num12(std::string_view code, std::string_view issuer) {
  STELLAR_TRY_ASSIGN(auto acc, issuer_to_account(issuer));
  xdr::AssetCode12 c{};
  std::memcpy(c.data(), code.data(), code.size());
  return xdr::AlphaNum12{c, acc};
}

bool valid_code(std::string_view code) {
  if (code.empty() || code.size() > 12) return false;
  return std::all_of(code.begin(), code.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '/' || c == '-' || c == '.' || c == '_';
  });
}

}  // namespace

Asset Asset::native() { return Asset{}; }

Result<Asset> Asset::credit(std::string_view code, std::string_view issuer) {
  if (!valid_code(code)) {
    return Error::invalid_argument("asset code must be 1-12 alphanumeric/./-/_ chars");
  }
  auto sk = StrKey::decode(issuer);
  if (!sk.ok()) return sk.error();
  if (sk->kind() != StrKeyKind::kAccount) {
    return Error{ErrorCode::kInvalidAddress, "asset issuer must be a G... account"};
  }
  Asset a;
  a.kind_ = code.size() <= 4 ? Kind::kAlphaNum4 : Kind::kAlphaNum12;
  a.code_ = std::string(code);
  a.issuer_ = std::string(issuer);
  return a;
}

Result<Asset> Asset::parse(std::string_view text) {
  if (text == "native") return native();
  const auto colon = text.find(':');
  if (colon == std::string_view::npos) {
    return Error::invalid_argument("asset must be 'native' or 'CODE:ISSUER'");
  }
  return credit(text.substr(0, colon), text.substr(colon + 1));
}

std::string Asset::to_string() const {
  if (is_native()) return "native";
  return code_ + ":" + issuer_;
}

Result<xdr::Asset> Asset::to_xdr() const {
  switch (kind_) {
    case Kind::kNative:
      return xdr::Asset::assetTypeNative();
    case Kind::kAlphaNum4: {
      STELLAR_TRY_ASSIGN(auto a, make_num4(code_, issuer_));
      return xdr::Asset::assetTypeCreditAlphanum4(a);
    }
    case Kind::kAlphaNum12: {
      STELLAR_TRY_ASSIGN(auto a, make_num12(code_, issuer_));
      return xdr::Asset::assetTypeCreditAlphanum12(a);
    }
  }
  return Error::internal("asset kind");
}

Result<xdr::ChangeTrustAsset> Asset::to_change_trust_asset() const {
  switch (kind_) {
    case Kind::kNative:
      return xdr::ChangeTrustAsset::assetTypeNative();
    case Kind::kAlphaNum4: {
      STELLAR_TRY_ASSIGN(auto a, make_num4(code_, issuer_));
      return xdr::ChangeTrustAsset::assetTypeCreditAlphanum4(a);
    }
    case Kind::kAlphaNum12: {
      STELLAR_TRY_ASSIGN(auto a, make_num12(code_, issuer_));
      return xdr::ChangeTrustAsset::assetTypeCreditAlphanum12(a);
    }
  }
  return Error::internal("asset kind");
}

Result<xdr::TrustLineAsset> Asset::to_trust_line_asset() const {
  switch (kind_) {
    case Kind::kNative:
      return xdr::TrustLineAsset::assetTypeNative();
    case Kind::kAlphaNum4: {
      STELLAR_TRY_ASSIGN(auto a, make_num4(code_, issuer_));
      return xdr::TrustLineAsset::assetTypeCreditAlphanum4(a);
    }
    case Kind::kAlphaNum12: {
      STELLAR_TRY_ASSIGN(auto a, make_num12(code_, issuer_));
      return xdr::TrustLineAsset::assetTypeCreditAlphanum12(a);
    }
  }
  return Error::internal("asset kind");
}

int Asset::compare(const Asset& o) const {
  const int ka = kind_ == Kind::kNative ? 0 : kind_ == Kind::kAlphaNum4 ? 1 : 2;
  const int kb = o.kind_ == Kind::kNative ? 0 : o.kind_ == Kind::kAlphaNum4 ? 1 : 2;
  if (ka != kb) return ka - kb;
  if (code_ != o.code_) return code_ < o.code_ ? -1 : 1;
  if (issuer_ != o.issuer_) return issuer_ < o.issuer_ ? -1 : 1;
  return 0;
}

Result<Asset> Asset::from_xdr(const xdr::Asset& xa) {
  switch (xa.type()) {
    case xdr::AssetType::ASSET_TYPE_NATIVE:
      return native();
    case xdr::AssetType::ASSET_TYPE_CREDIT_ALPHANUM4: {
      const auto& a = xa.alphaNum4();
      std::string code(reinterpret_cast<const char*>(a.assetCode.data()));
      code = code.c_str();  // stop at first NUL
      const auto* pk = a.issuer.get_if_ed25519();
      if (!pk) return Error::invalid_argument("alphanum4 issuer is not ed25519");
      StrKey sk{StrKeyKind::kAccount, *pk};
      return credit(code, sk.encode());
    }
    case xdr::AssetType::ASSET_TYPE_CREDIT_ALPHANUM12: {
      const auto& a = xa.alphaNum12();
      std::string code(reinterpret_cast<const char*>(a.assetCode.data()));
      code = code.c_str();
      const auto* pk = a.issuer.get_if_ed25519();
      if (!pk) return Error::invalid_argument("alphanum12 issuer is not ed25519");
      StrKey sk{StrKeyKind::kAccount, *pk};
      return credit(code, sk.encode());
    }
    default:
      return Error::invalid_argument("unsupported asset type");
  }
}

Result<std::array<std::uint8_t, 32>> Asset::liquidity_pool_id(const Asset& a, const Asset& b) {
  // Protocol orders assets; A is the smaller.
  const bool swap = b < a;
  const Asset& aa = swap ? b : a;
  const Asset& bb = swap ? a : b;
  STELLAR_TRY_ASSIGN(auto xa, aa.to_xdr());
  STELLAR_TRY_ASSIGN(auto xb, bb.to_xdr());
  xdr::LiquidityPoolConstantProductParameters cp{xdr::Asset{xa}, xdr::Asset{xb}, 30};
  xdr::LiquidityPoolParameters lp =
      xdr::LiquidityPoolParameters::liquidityPoolConstantProduct(cp);
  STELLAR_TRY_ASSIGN(Bytes bytes, xdr::encode(lp));
  return sha256(ByteSpan(bytes.data(), bytes.size()));
}

Result<std::array<std::uint8_t, 32>> Asset::sac_contract_id(const Network& n) const {
  STELLAR_TRY_ASSIGN(auto xa, to_xdr());
  xdr::HashIDPreimageContractId pc{
      xdr::uint256{n.id()}, xdr::ContractIDPreimage::contractIdPreimageFromAsset(xa)};
  xdr::HashIDPreimage pre = xdr::HashIDPreimage::envelopeTypeContractId(pc);
  STELLAR_TRY_ASSIGN(Bytes bytes, xdr::encode(pre));
  return sha256(ByteSpan(bytes.data(), bytes.size()));
}

}  // namespace stellar::inline v1
