// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/tx/transaction.hpp"

#include "stellar/util/encoding.hpp"
#include "stellar/xdr/codec.hpp"

namespace stellar::inline v1 {

namespace {

Result<xdr::MuxedAccount> to_muxed(const Address& a) { return a.to_muxed_account(); }

/// Builds the TransactionSignaturePayload for hashing/signing.
Result<xdr::TransactionSignaturePayload> sig_payload(const Network& network,
                                                     xdr::TransactionSignaturePayloadTaggedTransaction tagged) {
  xdr::TransactionSignaturePayload p;
  p.networkId = xdr::Hash{network.id()};
  p.taggedTransaction = std::move(tagged);
  return p;
}

/// Protocol-defined v0 -> v1 conversion used for signing/hash: the ed25519
/// source becomes a muxed ed25519 account, optional timeBounds becomes
/// PRECOND_TIME (or PRECOND_NONE), everything else carries over.
xdr::Transaction convert_v0(const xdr::TransactionV0& v0) {
  xdr::Transaction t;
  t.sourceAccount = xdr::MuxedAccount::keyTypeEd25519(xdr::uint256{v0.sourceAccountEd25519});
  t.fee = v0.fee;
  t.seqNum = v0.seqNum;
  t.cond = v0.timeBounds ? xdr::Preconditions::precondTime(*v0.timeBounds)
                         : xdr::Preconditions::precondNone();
  t.memo = v0.memo;
  t.operations = v0.operations;
  t.ext = xdr::TransactionExt::v0();
  return t;
}

}  // namespace

// ---------------------------------------------------------------------------
// TransactionBuilder
// ---------------------------------------------------------------------------

TransactionBuilder::TransactionBuilder(const Address& source) {
  if (auto m = source.to_muxed_account(); m.ok()) {
    tx_.sourceAccount = *m;
  }
  tx_.cond = xdr::Preconditions::precondNone();
  tx_.memo = xdr::Memo::memoNone();
  tx_.ext = xdr::TransactionExt::v0();
}

namespace {
/// Migrates a PRECOND_TIME cond into V2 preserving its timeBounds, or seeds a
/// fresh V2. Returns the V2 struct to mutate.
xdr::PreconditionsV2 ensure_v2(xdr::Preconditions& cond) {
  xdr::PreconditionsV2 v2{};
  if (cond.type() == xdr::PreconditionType::PRECOND_V2) {
    v2 = cond.v2();
  } else if (cond.type() == xdr::PreconditionType::PRECOND_TIME) {
    v2.timeBounds = cond.timeBounds();
  }
  return v2;
}
}  // namespace

TransactionBuilder& TransactionBuilder::time_bounds(std::uint64_t lo, std::uint64_t hi) {
  const xdr::TimeBounds tb{static_cast<xdr::uint64>(lo), static_cast<xdr::uint64>(hi)};
  if (tx_.cond.type() == xdr::PreconditionType::PRECOND_V2) {
    auto v2 = tx_.cond.v2();
    v2.timeBounds = tb;
    tx_.cond = xdr::Preconditions::precondV2(v2);
  } else {
    tx_.cond = xdr::Preconditions::precondTime(tb);
  }
  return *this;
}

TransactionBuilder& TransactionBuilder::ledger_bounds(std::uint32_t lo, std::uint32_t hi) {
  auto v2 = ensure_v2(tx_.cond);
  v2.ledgerBounds = xdr::LedgerBounds{lo, hi};
  tx_.cond = xdr::Preconditions::precondV2(v2);
  return *this;
}

TransactionBuilder& TransactionBuilder::min_sequence_number(std::int64_t seq) {
  auto v2 = ensure_v2(tx_.cond);
  v2.minSeqNum = static_cast<xdr::int64>(seq);
  tx_.cond = xdr::Preconditions::precondV2(v2);
  return *this;
}

TransactionBuilder& TransactionBuilder::min_sequence_age(std::uint64_t seconds) {
  auto v2 = ensure_v2(tx_.cond);
  v2.minSeqAge = static_cast<xdr::uint64>(seconds);
  tx_.cond = xdr::Preconditions::precondV2(v2);
  return *this;
}

TransactionBuilder& TransactionBuilder::min_sequence_ledger_gap(std::uint32_t ledgers) {
  auto v2 = ensure_v2(tx_.cond);
  v2.minSeqLedgerGap = ledgers;
  tx_.cond = xdr::Preconditions::precondV2(v2);
  return *this;
}

TransactionBuilder& TransactionBuilder::extra_signer(xdr::SignerKey key) {
  auto v2 = ensure_v2(tx_.cond);
  if (v2.extraSigners.size() >= 2) {
    if (!error_) {
      error_ = Error::invalid_argument("PRECOND_V2 allows at most 2 extra signers");
    }
  } else {
    v2.extraSigners.push_back(key);
    tx_.cond = xdr::Preconditions::precondV2(v2);
  }
  return *this;
}

TransactionBuilder& TransactionBuilder::add_operation(xdr::Operation op) {
  tx_.operations.push_back(std::move(op));
  return *this;
}

TransactionBuilder& TransactionBuilder::no_timeout() noexcept {
  no_timeout_ = true;
  return *this;
}

Result<xdr::Transaction> TransactionBuilder::build() const {
  if (error_) return *error_;
  if (tx_.operations.empty()) {
    return Error::invalid_argument("transaction needs at least one operation");
  }
  if (tx_.operations.size() > 100) {
    return Error::invalid_argument("transaction exceeds the 100-operation limit");
  }
  // time bounds are mandatory unless explicitly opted out
  if (!no_timeout_) {
    const bool has_time =
        (tx_.cond.type() == xdr::PreconditionType::PRECOND_TIME) ||
        (tx_.cond.type() == xdr::PreconditionType::PRECOND_V2 &&
         tx_.cond.v2().timeBounds.has_value());
    if (!has_time) {
      return Error::invalid_argument(
          "time bounds are required; call no_timeout() to opt out");
    }
  }
  return tx_;
}

// ---------------------------------------------------------------------------
// FeeBumpBuilder
// ---------------------------------------------------------------------------

FeeBumpBuilder::FeeBumpBuilder(const Address& fee_source) {
  if (auto m = fee_source.to_muxed_account(); m.ok()) {
    tx_.feeSource = *m;
  }
  tx_.ext = xdr::FeeBumpTransactionExt::v0();
}

Result<xdr::FeeBumpTransaction> FeeBumpBuilder::build(xdr::Transaction inner) const {
  xdr::FeeBumpTransaction out = tx_;
  out.innerTx = xdr::FeeBumpTransactionInnerTx::envelopeTypeTx(
      xdr::TransactionV1Envelope{std::move(inner), {}});
  return out;
}

Result<xdr::FeeBumpTransaction>
FeeBumpBuilder::build(const TransactionEnvelope& inner) const {
  const auto* tx = inner.transaction();
  if (!tx) {
    return Error::invalid_argument("fee-bump inner must be a v1 transaction");
  }
  xdr::FeeBumpTransaction out = tx_;
  xdr::TransactionV1Envelope v1;
  v1.tx = *tx;
  v1.signatures.assign(inner.signatures().begin(), inner.signatures().end());
  out.innerTx = xdr::FeeBumpTransactionInnerTx::envelopeTypeTx(std::move(v1));
  return out;
}

// ---------------------------------------------------------------------------
// TransactionEnvelope
// ---------------------------------------------------------------------------

TransactionEnvelope TransactionEnvelope::of_transaction(xdr::Transaction tx) {
  return TransactionEnvelope(
      xdr::TransactionEnvelope::envelopeTypeTx(xdr::TransactionV1Envelope{std::move(tx), {}}));
}

TransactionEnvelope TransactionEnvelope::of_fee_bump(xdr::FeeBumpTransaction tx) {
  return TransactionEnvelope(xdr::TransactionEnvelope::envelopeTypeTxFeeBump(
      xdr::FeeBumpTransactionEnvelope{std::move(tx), {}}));
}

Result<TransactionEnvelope> TransactionEnvelope::from_bytes(ByteSpan bytes) {
  STELLAR_TRY_ASSIGN(auto env, xdr::decode<xdr::TransactionEnvelope>(bytes));
  return TransactionEnvelope(std::move(env));
}

Result<TransactionEnvelope> TransactionEnvelope::from_base64(std::string_view b64) {
  STELLAR_TRY_ASSIGN(auto bytes, base64_decode(b64));
  return from_bytes(bytes);
}

const xdr::Transaction* TransactionEnvelope::transaction() const noexcept {
  switch (env_.type()) {
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX:
      return &env_.v1().tx;
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX_FEE_BUMP:
      if (const auto* v = env_.feeBump().tx.innerTx.get_if_v1()) return &v->tx;
      return nullptr;
    default:
      return nullptr;
  }
}

const xdr::FeeBumpTransaction* TransactionEnvelope::fee_bump() const noexcept {
  if (env_.type() == xdr::EnvelopeType::ENVELOPE_TYPE_TX_FEE_BUMP) return &env_.feeBump().tx;
  return nullptr;
}

Result<Bytes> TransactionEnvelope::signature_preimage(const Network& network) const {
  xdr::TransactionSignaturePayloadTaggedTransaction tagged;
  switch (env_.type()) {
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX:
      tagged = xdr::TransactionSignaturePayloadTaggedTransaction::envelopeTypeTx(env_.v1().tx);
      break;
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX_FEE_BUMP:
      tagged = xdr::TransactionSignaturePayloadTaggedTransaction::envelopeTypeTxFeeBump(
          env_.feeBump().tx);
      break;
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX_V0:
      // protocol converts v0 -> v1 for the signature hash
      tagged = xdr::TransactionSignaturePayloadTaggedTransaction::envelopeTypeTx(
          convert_v0(env_.v0().tx));
      break;
    default:
      return Error::invalid_argument("unsupported envelope type for signing");
  }
  STELLAR_TRY_ASSIGN(auto payload, sig_payload(network, std::move(tagged)));
  return xdr::encode(payload);
}

Result<std::array<std::uint8_t, 32>> TransactionEnvelope::signature_base(
    const Network& network) const {
  STELLAR_TRY_ASSIGN(auto pre, signature_preimage(network));
  return sha256(ByteSpan(pre.data(), pre.size()));
}

void TransactionEnvelope::load_sigs() {
  signatures_.clear();
  switch (env_.type()) {
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX_V0:
      for (const auto& s : env_.v0().signatures) signatures_.push_back(s);
      break;
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX:
      for (const auto& s : env_.v1().signatures) signatures_.push_back(s);
      break;
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX_FEE_BUMP:
      for (const auto& s : env_.feeBump().signatures) signatures_.push_back(s);
      break;
    default:
      break;
  }
}

void TransactionEnvelope::store_sigs() {
  switch (env_.type()) {
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX_V0: {
      auto& vec = env_.v0().signatures;
      vec.clear();
      for (const auto& s : signatures_) vec.push_back(s);
      break;
    }
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX: {
      auto& vec = env_.v1().signatures;
      vec.clear();
      for (const auto& s : signatures_) vec.push_back(s);
      break;
    }
    case xdr::EnvelopeType::ENVELOPE_TYPE_TX_FEE_BUMP: {
      auto& vec = env_.feeBump().signatures;
      vec.clear();
      for (const auto& s : signatures_) vec.push_back(s);
      break;
    }
    default:
      break;
  }
}

Result<void> TransactionEnvelope::sign(const Signer& signer, const Network& network) {
  STELLAR_TRY_ASSIGN(auto base, signature_base(network));
  STELLAR_TRY_ASSIGN(auto sig, signer.sign(ByteSpan(base.data(), base.size())));
  xdr::DecoratedSignature d;
  d.hint = xdr::SignatureHint{signer.signature_hint()};
  d.signature = xdr::Signature(sig.begin(), sig.end());
  signatures_.push_back(d);
  store_sigs();
  return {};
}

Result<void> TransactionEnvelope::add_decorated_signature(xdr::DecoratedSignature sig) {
  signatures_.push_back(std::move(sig));
  store_sigs();
  return {};
}

Result<Bytes> TransactionEnvelope::to_bytes() const { return xdr::encode(env_); }

Result<std::string> TransactionEnvelope::to_base64() const {
  STELLAR_TRY_ASSIGN(auto bytes, to_bytes());
  return base64_encode(ByteSpan(bytes.data(), bytes.size()));
}

// ---------------------------------------------------------------------------
// SignatureHelpers
// ---------------------------------------------------------------------------

Result<xdr::DecoratedSignature> SignatureHelpers::sign(const Signer& signer,
                                                     ByteSpan signature_base) {
  STELLAR_TRY_ASSIGN(auto sig, signer.sign(signature_base));
  xdr::DecoratedSignature d;
  d.hint = xdr::SignatureHint{signer.signature_hint()};
  d.signature = xdr::Signature(sig.begin(), sig.end());
  return d;
}

xdr::DecoratedSignature SignatureHelpers::hash_x(std::array<std::uint8_t, 32> preimage) {
  xdr::DecoratedSignature d;
  // hash-x hint = last 4 bytes of the hash (sha256 of preimage)
  const auto h = sha256(ByteSpan(preimage.data(), preimage.size()));
  d.hint = xdr::SignatureHint{{h[28], h[29], h[30], h[31]}};
  d.signature = xdr::Signature(preimage.begin(), preimage.end());
  return d;
}

Result<xdr::SignerKey>
SignatureHelpers::signed_payload_signer_key(const Address& account, ByteSpan payload) {
  if (payload.empty() || payload.size() > 64) {
    return Error::invalid_argument("signed payload must be 1..64 bytes");
  }
  STELLAR_TRY_ASSIGN(auto mx, account.to_muxed_account());
  xdr::uint256 pk{};
  if (mx.type() == xdr::CryptoKeyType::KEY_TYPE_ED25519) {
    pk = mx.ed25519();
  } else if (mx.type() == xdr::CryptoKeyType::KEY_TYPE_MUXED_ED25519) {
    pk = mx.med25519().ed25519;
  } else {
    return Error::invalid_argument("signed payload requires an ed25519 account");
  }
  xdr::SignerKeyEd25519SignedPayload sp{pk, {}};
  sp.payload.assign(payload.begin(), payload.end());
  return xdr::SignerKey::signerKeyTypeEd25519SignedPayload(sp);
}

Result<xdr::DecoratedSignature>
SignatureHelpers::signed_payload_signature(const Signer& signer,
                                           const xdr::SignerKey& key,
                                           ByteSpan payload, ByteSpan message) {
  // CAP-40: the signed data is payload ++ message
  Bytes data(payload.begin(), payload.end());
  data.insert(data.end(), message.begin(), message.end());
  STELLAR_TRY_ASSIGN(auto sig, signer.sign(ByteSpan(data.data(), data.size())));
  // hint = last 4 bytes of the encoded signed-payload signer key
  STELLAR_TRY_ASSIGN(auto key_bytes, xdr::encode(key));
  const auto n = key_bytes.size();
  xdr::DecoratedSignature d;
  d.hint = xdr::SignatureHint{{key_bytes[n - 4], key_bytes[n - 3], key_bytes[n - 2],
                             key_bytes[n - 1]}};
  d.signature = xdr::Signature(sig.begin(), sig.end());
  return d;
}

}  // namespace stellar::inline v1
