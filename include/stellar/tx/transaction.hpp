// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file transaction.hpp
/// Transaction building, envelope wrapping, and network-bound signing.
/// Signature model: signature_base = SHA-256(XDR TransactionSignaturePayload)
/// where the payload embeds the SHA-256 network ID — always explicit.

#include "stellar/core/address.hpp"
#include "stellar/core/keypair.hpp"
#include "stellar/core/memo.hpp"
#include "stellar/core/network.hpp"
#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1 {

/// Builder for a classic Transaction (the `v1` arm of TransactionEnvelope).
class STELLAR_API TransactionBuilder {
 public:
  /// `source` may be a plain or muxed account (G.../M...).
  explicit TransactionBuilder(const Address& source);

  TransactionBuilder& sequence_number(std::int64_t seq) {
    tx_.seqNum = static_cast<xdr::int64>(seq);
    return *this;
  }
  /// Total fee in stroops (inclusion fee = fee * ops). Minimum 100 per op.
  TransactionBuilder& fee(std::uint32_t fee) {
    tx_.fee = fee;
    return *this;
  }
  TransactionBuilder& memo(const Memo& memo) {
    tx_.memo = memo.xdr();
    return *this;
  }
  /// Replaces the whole precondition object (use builder helpers below).
  TransactionBuilder& preconditions(xdr::Preconditions cond) {
    tx_.cond = std::move(cond);
    return *this;
  }
  /// Time bounds validity window (unix seconds, [min,max)).
  TransactionBuilder& time_bounds(std::uint64_t min_time, std::uint64_t max_time);
  TransactionBuilder& ledger_bounds(std::uint32_t min_ledger, std::uint32_t max_ledger);
  TransactionBuilder& min_sequence_number(std::int64_t seq);
  TransactionBuilder& min_sequence_age(std::uint64_t seconds);
  TransactionBuilder& min_sequence_ledger_gap(std::uint32_t ledgers);
  /// Adds a required extra signer (protocol max 2). Excess is recorded and
  /// reported by build() rather than silently dropped.
  TransactionBuilder& extra_signer(xdr::SignerKey key);

  /// Explicitly opt out of time bounds. Without this (or time_bounds()) the
  /// builder errors at build() — mandatory-bounds is the network's default
  /// posture, opting out must be a deliberate choice.
  TransactionBuilder& no_timeout() noexcept;

  TransactionBuilder& add_operation(xdr::Operation op);

  /// Finalizes — at least one operation and a source account are required;
  /// also reports any precondition errors recorded along the way.
  Result<xdr::Transaction> build() const;

 private:
  xdr::Transaction tx_{};
  std::optional<Error> error_;
  bool no_timeout_ = false;
};

/// Builder for a fee-bump transaction wrapping an existing v1 transaction.
class TransactionEnvelope;
class STELLAR_API FeeBumpBuilder {
 public:
  /// `fee_source` pays the outer fee; `fee` is the TOTAL stroop fee (must be
  /// >= inner fee + 100*inner_ops).
  explicit FeeBumpBuilder(const Address& fee_source);
  FeeBumpBuilder& fee(std::int64_t total_fee) {
    tx_.fee = static_cast<xdr::int64>(total_fee);
    return *this;
  }
  /// Wrap an inner transaction envelope — its signatures travel inside the
  /// bump's `innerTx` (they're the inner envelope's signatures, not the
  /// outer's).
  Result<xdr::FeeBumpTransaction> build(const TransactionEnvelope& inner) const;
  /// Convenience for an unsigned inner transaction (signatures empty).
  Result<xdr::FeeBumpTransaction> build(xdr::Transaction inner) const;

 private:
  xdr::FeeBumpTransaction tx_{};
};

/// A transaction or fee-bump envelope: build, sign, inspect, serialize.
class STELLAR_API TransactionEnvelope {
 public:
  /// Wrap a built v1 transaction.
  static TransactionEnvelope of_transaction(xdr::Transaction tx);
  /// Wrap a built fee-bump transaction.
  static TransactionEnvelope of_fee_bump(xdr::FeeBumpTransaction tx);
  /// Decode from XDR bytes or base64 envelope XDR.
  static Result<TransactionEnvelope> from_bytes(ByteSpan bytes);
  static Result<TransactionEnvelope> from_base64(std::string_view b64);

  [[nodiscard]] const xdr::TransactionEnvelope& xdr() const noexcept { return env_; }
  [[nodiscard]] xdr::EnvelopeType type() const noexcept { return env_.type(); }

  /// The inner transaction (v1 or fee-bump inner); nullptr for v0.
  [[nodiscard]] const xdr::Transaction* transaction() const noexcept;
  [[nodiscard]] const xdr::FeeBumpTransaction* fee_bump() const noexcept;

  /// Raw signature preimage: XDR(TransactionSignaturePayload).
  [[nodiscard]] Result<Bytes> signature_preimage(const Network& network) const;
  /// SHA-256(preimage) — what signers sign and what serves as the tx hash.
  [[nodiscard]] Result<std::array<std::uint8_t, 32>> signature_base(const Network& network) const;
  /// The transaction hash (same as signature_base for v1/fee-bump).
  [[nodiscard]] Result<std::array<std::uint8_t, 32>> hash(const Network& network) const {
    return signature_base(network);
  }

  /// Appends a decorated signature for `signer` on `network`. For fee-bump
  /// envelopes the signature goes to the outer fee-bump signature list.
  Result<void> sign(const Signer& signer, const Network& network);
  /// Append a raw decorated signature (e.g. from an external signer).
  Result<void> add_decorated_signature(xdr::DecoratedSignature sig);

  [[nodiscard]] const std::vector<xdr::DecoratedSignature>& signatures() const noexcept {
    return signatures_;
  }

  [[nodiscard]] Result<Bytes> to_bytes() const;
  [[nodiscard]] Result<std::string> to_base64() const;

 private:
  explicit TransactionEnvelope(xdr::TransactionEnvelope e) : env_(std::move(e)) { load_sigs(); }
  void load_sigs();
  void store_sigs();

  xdr::TransactionEnvelope env_;
  std::vector<xdr::DecoratedSignature> signatures_;  // shadows the bounded vec
};

/// A decorated signature: hint (last 4 bytes of signer pubkey) + 64-byte sig.
/// Helpers to build non-keypair decorated signatures (hash-x, pre-auth,
/// signed payload) used by multi-sig flows.
struct STELLAR_API SignatureHelpers {
  /// Ed25519 decorated signature from a keypair.
  static Result<xdr::DecoratedSignature> sign(const Signer& signer, ByteSpan signature_base);
  /// SHA-256-hash-x signature (the preimage IS the signature).
  static xdr::DecoratedSignature hash_x(std::array<std::uint8_t, 32> preimage);
  /// CAP-40 ed25519-signed-payload signature: signs `payload ++ message` and
  /// carries the signed-payload signer-key hint (last 4 bytes of the encoded
  /// key). `key` is the signer key built by signed_payload_signer_key().
  static Result<xdr::DecoratedSignature> signed_payload_signature(const Signer& signer,
                                                                  const xdr::SignerKey& key,
                                                                  ByteSpan payload,
                                                                  ByteSpan message);
  /// Construct the ED25519_SIGNED_PAYLOAD signer key for an account + payload.
  static Result<xdr::SignerKey> signed_payload_signer_key(const Address& account, ByteSpan payload);
};

}  // namespace stellar::inline v1
