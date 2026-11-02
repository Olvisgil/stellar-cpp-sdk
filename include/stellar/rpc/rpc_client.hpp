// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file rpc_client.hpp
/// Synchronous Stellar RPC (JSON-RPC 2.0) client over a pluggable
/// HttpTransport. XDR payloads arrive as base64 and decode into the
/// generated `stellar::xdr` types.

#include "stellar/core/address.hpp"
#include "stellar/core/keypair.hpp"
#include "stellar/core/network.hpp"
#include "stellar/export.hpp"
#include "stellar/http.hpp"
#include "stellar/json.hpp"
#include "stellar/result.hpp"
#include "stellar/tx/transaction.hpp"
#include "stellar/xdr/generated/stellar_ledger.hpp"
#include "stellar/xdr/generated/stellar_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1 {

struct STELLAR_API LedgerEntryRow {
  xdr::LedgerKey key;
  xdr::LedgerEntryData data;
  std::uint32_t last_modified_ledger{0};
  std::uint32_t live_until_ledger{0};  // classic entries report latest ledger
  /// Populated when decode fails — raw b64 + error so one unfamiliar arm after
  /// a protocol upgrade doesn't sink the whole response (lazy-decode escape).
  std::optional<std::string> raw_xdr;
  std::optional<Error> decode_error;
};

struct STELLAR_API LedgerEntriesResponse {
  std::vector<LedgerEntryRow> entries;
  std::uint32_t latest_ledger{0};
};

struct STELLAR_API LedgerRow {
  std::uint32_t sequence{0};
  std::string hash;
  std::uint64_t close_time{0};
  xdr::LedgerCloseMeta meta;
};

struct STELLAR_API LedgersResponse {
  std::vector<LedgerRow> ledgers;
  std::uint32_t latest_ledger{0};
  std::uint64_t cursor{0};
};

struct STELLAR_API EventRow {
  std::string type;  // contract | system | diagnostic
  std::uint32_t ledger{0};
  std::string ledger_closed_at;
  std::string contract_id;
  std::string id;
  std::string tx_hash;
  bool in_successful_contract_call{false};
  std::vector<xdr::SCVal> topic;
  xdr::SCVal value;
};

struct STELLAR_API EventsResponse {
  std::vector<EventRow> events;
  std::uint32_t latest_ledger{0};
  std::string cursor;
};

struct STELLAR_API TransactionResponse {
  bool found{false};
  std::string status;  // SUCCESS | FAILED | NOT_FOUND
  std::uint32_t ledger{0};
  std::uint32_t application_order{0};
  std::string ledger_closed_at;
  std::string tx_hash;
  std::optional<xdr::TransactionResult> result;
  std::optional<xdr::TransactionMeta> meta;
  std::optional<xdr::TransactionEnvelope> envelope;
};

struct STELLAR_API SendTransactionResponse {
  std::string status;  // PENDING | DUPLICATE | TRY_AGAIN_LATER | ERROR
  std::string hash;
  std::optional<xdr::TransactionResult> error_result;
  std::vector<std::string> diagnostic_events_xdr;
};

struct STELLAR_API SimulateTransactionResponse {
  std::optional<std::string> error;
  std::int64_t min_resource_fee{0};
  std::optional<xdr::TransactionResult> result;
  std::optional<xdr::SorobanTransactionData> transaction_data;
  /// Per-invoke-host-function return values (decoded SCVals from results[].xdr).
  std::vector<xdr::SCVal> results;
  std::vector<xdr::SorobanAuthorizationEntry> auth;
  std::vector<std::string> events_xdr;  // diagnostic events (raw xdr b64)

  /// populate when simulation says state needs restoring
  struct RestorePreamble {
    xdr::SorobanTransactionData transaction_data;
    std::int64_t min_resource_fee{0};
  };
  std::optional<RestorePreamble> restore_preamble;
};

/// One RPC fee-stat bucket: fields mirror getFeeStats (values may be numeric
/// strings on the wire; parsed to integers here).
struct STELLAR_API FeeBucket {
  std::uint64_t min{0}, mode{0}, p10{0}, p50{0}, p90{0}, p95{0}, p99{0}, max{0};
  std::uint64_t transaction_count{0}, ledger_count{0};
};

struct STELLAR_API FeeStats {
  FeeBucket soroban_inclusion;
  FeeBucket inclusion;
};

struct STELLAR_API VersionInfo {
  std::string version, commit_hash, captive_core_version, friendbot_url;
  std::uint32_t protocol_version{0};
};

/// Synchronous JSON-RPC client. Thread-safe for concurrent calls.
class STELLAR_API RpcClient {
 public:
  RpcClient(HttpTransport& transport, std::string endpoint)
      : transport_(transport), endpoint_(std::move(endpoint)) {}

  [[nodiscard]] Result<bool> get_health();
  [[nodiscard]] Result<std::uint32_t> get_latest_ledger_sequence();
  [[nodiscard]] Result<VersionInfo> get_version_info();
  [[nodiscard]] Result<FeeStats> get_fee_stats();
  [[nodiscard]] Result<Json> get_network();

  [[nodiscard]] Result<LedgerEntriesResponse> get_ledger_entries(
      const std::vector<xdr::LedgerKey>& keys);

  [[nodiscard]] Result<LedgersResponse> get_ledgers(std::uint32_t start_sequence,
                                                    std::uint32_t limit = 10);

  /// One event filter spec — RPC accepts several in a single call.
  struct STELLAR_API EventsFilter {
    EventsFilter() = default;
    std::optional<std::string> type;             // "contract" | "system" | "diagnostic"
    std::vector<std::string> contract_ids;       // C... strkeys
    std::vector<std::vector<xdr::SCVal>> topic;  // per-filter topic patterns
    std::uint32_t limit = 50;
  };
  /// Multiple filters are OR'd by the RPC; empty vector matches all events.
  [[nodiscard]] Result<EventsResponse> get_events(std::uint32_t start_ledger,
                                                  std::vector<EventsFilter> filters = {});

  [[nodiscard]] Result<TransactionResponse> get_transaction(std::string_view tx_hash);

  /// Submit an already-signed envelope.
  [[nodiscard]] Result<SendTransactionResponse> send_transaction(
      const TransactionEnvelope& envelope);

  /// Simulate a (possibly unsigned) transaction.
  [[nodiscard]] Result<SimulateTransactionResponse> simulate_transaction(
      const xdr::Transaction& tx);
  [[nodiscard]] Result<SimulateTransactionResponse> simulate_envelope(
      const TransactionEnvelope& envelope);

  /// send + poll get_transaction until SUCCESS/FAILED (or attempts run out).
  /// Options for simulateTransaction.
  struct SimulateOptions {
    std::optional<std::uint32_t> instruction_leeway;
    std::optional<std::string> auth_mode;  // "enforce" | "record" | "record_allow_nonroot"
    bool use_upgraded_auth = false;
    bool xdr_format_json = false;  // server returns xdrFormat=json
  };
  /// Simulate with options. `envelope` may carry signatures or not.
  [[nodiscard]] Result<SimulateTransactionResponse> simulate(const TransactionEnvelope& envelope,
                                                             const SimulateOptions& opts);

  /// Batch getTransactions — one RPC call per chunk of 50 hashes.
  [[nodiscard]] Result<std::vector<TransactionResponse>> get_transactions(
      const std::vector<std::string>& tx_hashes);

  /// queryEvents (beta): same shape as getEvents but uses the beta endpoint —
  /// call only against servers that advertise support.
  [[nodiscard]] Result<EventsResponse> query_events(std::uint32_t start_ledger,
                                                    std::vector<EventsFilter> filters = {});

  /// Convenience: fetch an account's AccountEntry (seqNum, balances) — wraps
  /// get_ledger_entries on the account key.
  [[nodiscard]] Result<xdr::AccountEntry> get_account(const Address& account);

  /// Contract data lookup: resolves (contractId, SCVal key) -> LedgerEntryData
  /// of the contractData entry (must be CONTRACT_DATA arm).
  [[nodiscard]] Result<xdr::LedgerEntryData> get_contract_data(std::string_view contract_strkey,
                                                               const xdr::SCVal& key);

  /// Contract WASM code: contractId -> wasm-hash ledger key -> wasm bytes.
  [[nodiscard]] Result<Bytes> get_contract_code(std::string_view contract_strkey);

  /// Friendbot funding: GET {friendbot_url}?addr=G... Returns the body (JSON).
  [[nodiscard]] Result<Json> friendbot_fund(std::string_view friendbot_url, const Address& account);

  [[nodiscard]] Result<TransactionResponse> send_and_poll(const TransactionEnvelope& envelope,
                                                          int max_polls = 20,
                                                          std::uint32_t poll_interval_ms = 1500);

  /// Escape hatch: raw JSON-RPC call for methods not covered above.
  [[nodiscard]] Result<Json> call(std::string_view method, Json params);

 private:
  [[nodiscard]] Result<SimulateTransactionResponse> simulate_envelope_params(Json params);
  [[nodiscard]] static Result<LedgerEntriesResponse> parse_ledger_entries(const Json& r);
  [[nodiscard]] Result<Json> get_json(std::string_view url);

  HttpTransport& transport_;
  std::string endpoint_;
  std::atomic<std::int64_t> next_id_{1};
};

}  // namespace stellar::inline v1
