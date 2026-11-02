// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/core/address.hpp"

namespace stellar::inline v1 {

Result<Address> Address::parse(std::string_view text) {
  STELLAR_TRY_ASSIGN(auto sk, StrKey::decode(text));
  return from_strkey(std::move(sk));
}

Result<Address> Address::from_strkey(StrKey sk) {
  Address a;
  switch (sk.kind()) {
    case StrKeyKind::kAccount:
      a.kind_ = Kind::kAccount;
      a.id_ = *sk.fixed();
      return a;
    case StrKeyKind::kMuxedAccount: {
      const auto* m = sk.muxed();
      a.kind_ = Kind::kMuxedAccount;
      a.id_ = m->id;
      a.sub_id_ = m->sub_id;
      return a;
    }
    case StrKeyKind::kContract:
      a.kind_ = Kind::kContract;
      a.id_ = *sk.fixed();
      return a;
    case StrKeyKind::kMuxedContract: {
      const auto* m = sk.muxed();
      a.kind_ = Kind::kMuxedContract;
      a.id_ = m->id;
      a.sub_id_ = m->sub_id;
      return a;
    }
    default:
      return Error{ErrorCode::kInvalidStrKey, "strkey kind is not an address"};
  }
}

Address Address::contract(std::array<std::uint8_t, 32> id) {
  Address a;
  a.kind_ = Kind::kContract;
  a.id_ = id;
  return a;
}

std::string Address::to_string() const {
  StrKey sk;
  switch (kind_) {
    case Kind::kAccount:
      sk = StrKey{StrKeyKind::kAccount, id_};
      break;
    case Kind::kContract:
      sk = StrKey{StrKeyKind::kContract, id_};
      break;
    case Kind::kMuxedAccount:
      sk = StrKey{StrKeyKind::kMuxedAccount, StrKey::Muxed{id_, sub_id_.value_or(0)}};
      break;
    case Kind::kMuxedContract:
      sk = StrKey{StrKeyKind::kMuxedContract, StrKey::Muxed{id_, sub_id_.value_or(0)}};
      break;
  }
  return sk.encode();
}

Result<xdr::SCAddress> Address::to_sc_address() const {
  const xdr::uint256 h{id_};
  switch (kind_) {
    case Kind::kAccount:
      return xdr::SCAddress::scAddressTypeAccount(xdr::PublicKey::publicKeyTypeEd25519(h));
    case Kind::kContract:
      return xdr::SCAddress::scAddressTypeContract(xdr::ContractID{h});
    case Kind::kMuxedAccount:
      return xdr::SCAddress::scAddressTypeMuxedAccount(
          xdr::MuxedEd25519Account{static_cast<xdr::uint64>(sub_id_.value_or(0)), h});
    case Kind::kMuxedContract:
      return xdr::SCAddress::scAddressTypeMuxedContract(
          xdr::MuxedContract{static_cast<xdr::uint64>(sub_id_.value_or(0)), xdr::ContractID{h}});
  }
  return Error::internal("address kind");
}

Result<Address> Address::from_sc_address(const xdr::SCAddress& sa) {
  Address a;
  switch (sa.type()) {
    case xdr::SCAddressType::SC_ADDRESS_TYPE_ACCOUNT: {
      const auto& pk = sa.accountId();
      if (pk.type() != xdr::PublicKeyType::PUBLIC_KEY_TYPE_ED25519) {
        return Error::invalid_argument("SCAddress account is not ed25519");
      }
      a.kind_ = Kind::kAccount;
      a.id_ = pk.ed25519();
      return a;
    }
    case xdr::SCAddressType::SC_ADDRESS_TYPE_CONTRACT:
      a.kind_ = Kind::kContract;
      a.id_ = sa.contractId();
      return a;
    case xdr::SCAddressType::SC_ADDRESS_TYPE_MUXED_ACCOUNT: {
      const auto& m = sa.muxedAccount();
      a.kind_ = Kind::kMuxedAccount;
      a.id_ = m.ed25519;
      a.sub_id_ = static_cast<std::uint64_t>(m.id);
      return a;
    }
    case xdr::SCAddressType::SC_ADDRESS_TYPE_MUXED_CONTRACT: {
      const auto& m = sa.muxedContract();
      a.kind_ = Kind::kMuxedContract;
      a.id_ = m.contractId;
      a.sub_id_ = static_cast<std::uint64_t>(m.id);
      return a;
    }
    default:
      return Error::invalid_argument("unsupported SCAddress type");
  }
}

Result<xdr::MuxedAccount> Address::to_muxed_account() const {
  if (kind_ == Kind::kAccount) {
    return xdr::MuxedAccount::keyTypeEd25519(xdr::uint256{id_});
  }
  if (kind_ == Kind::kMuxedAccount) {
    xdr::MuxedAccountMed25519 med;
    med.id = static_cast<xdr::uint64>(sub_id_.value_or(0));
    med.ed25519 = xdr::uint256{id_};
    return xdr::MuxedAccount::keyTypeMuxedEd25519(med);
  }
  return Error::invalid_argument("only G/M addresses convert to MuxedAccount");
}

const std::array<std::uint8_t, 32>* Address::account_id_bytes() const {
  if (kind_ == Kind::kAccount || kind_ == Kind::kMuxedAccount) return &id_;
  return nullptr;
}

std::optional<std::uint64_t> Address::muxed_id() const {
  return sub_id_;
}

}  // namespace stellar::inline v1
