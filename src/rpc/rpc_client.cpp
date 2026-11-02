// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/rpc/rpc_client.hpp"

#include "stellar/util/encoding.hpp"
#include "stellar/xdr/codec.hpp"

#include <chrono>
#include <thread>

namespace stellar::inline v1 {

namespace {

// ---------------------------------------------------------------------------
// JSON field accessors with strict typing — fail loudly on shape mismatch.
// ---------------------------------------------------------------------------

Result<std::string> need_string(const Json& o, std::string_view key) {
  const auto& v = o.at(key);
  if (!v.is_string()) {
    return Error{ErrorCode::kProtocolError, "field '" + std::string(key) + "' is not a string"};
  }
  return v.as_string();
}

Result<std::uint32_t> need_u32(const Json& o, std::string_view key) {
  const auto& v = o.at(key);
  if (!v.is_number()) {
    return Error{ErrorCode::kProtocolError, "field '" + std::string(key) + "' is not a number"};
  }
  return static_cast<std::uint32_t>(v.as_int());
}

/// Empty array view for absent array fields.
const Json::Array& safe_array(const Json& j) {
  static const Json::Array empty;
  return j.is_array() ? j.as_array() : empty;
}

/// Decode an XDR type from a base64 string field.
template <typename T>
Result<T> xdr_field(const Json& o, std::string_view key) {
  const auto& v = o.at(key);
  if (!v.is_string()) {
    return Error{ErrorCode::kProtocolError,
                 "XDR field '" + std::string(key) + "' missing or not a string"};
  }
  STELLAR_TRY_ASSIGN(auto bytes, base64_decode(v.as_string()));
  return xdr::decode<T>(bytes);
}

template <typename T>
Result<T> xdr_b64(std::string_view b64) {
  STELLAR_TRY_ASSIGN(auto bytes, base64_decode(b64));
  return xdr::decode<T>(bytes);
}

}  // namespace

// ---------------------------------------------------------------------------
// transport
// ---------------------------------------------------------------------------

Result<Json> RpcClient::call(std::string_view method, Json params) {
  const std::int64_t id = next_id_.fetch_add(1);
  HttpRequest req;
  req.method = "POST";
  req.url = endpoint_;
  req.headers["Content-Type"] = "application/json";
  req.body = JsonRpc::request(method, std::move(params), id).dump();

  STELLAR_TRY_ASSIGN(auto res, transport_.perform(req));
  if (res.status < 200 || res.status >= 300) {
    return Error{ErrorCode::kHttpError, "RPC endpoint returned HTTP " + std::to_string(res.status)};
  }
  STELLAR_TRY_ASSIGN(auto doc, Json::parse(res.body));
  if (const auto* err = JsonRpc::error_of(doc)) {
    const std::string msg =
        err->at("message").is_string() ? err->at("message").as_string() : "rpc error";
    return Error{ErrorCode::kRpcError,
                 msg + " (code " + std::to_string(err->at("code").as_int()) + ")"};
  }
  const auto* result = JsonRpc::result_of(doc);
  if (!result) return Error{ErrorCode::kProtocolError, "response has no 'result'"};
  return *result;
}

// ---------------------------------------------------------------------------
// simple methods
// ---------------------------------------------------------------------------

Result<bool> RpcClient::get_health() {
  STELLAR_TRY_ASSIGN(auto r, call("getHealth", Json::object()));
  const auto& s = r.at("status");
  return s.is_string() && s.as_string() == "healthy";
}

Result<std::uint32_t> RpcClient::get_latest_ledger_sequence() {
  STELLAR_TRY_ASSIGN(auto r, call("getLatestLedger", Json::object()));
  return need_u32(r, "sequence");
}

Result<VersionInfo> RpcClient::get_version_info() {
  STELLAR_TRY_ASSIGN(auto r, call("getVersionInfo", Json::object()));
  VersionInfo v;
  if (auto s = need_string(r, "version"); s.ok()) v.version = *s;
  if (auto s = need_string(r, "commitHash"); s.ok()) v.commit_hash = *s;
  if (auto s = need_string(r, "captiveCoreVersion"); s.ok()) v.captive_core_version = *s;
  if (auto s = need_string(r, "friendbotUrl"); s.ok()) v.friendbot_url = *s;
  if (auto n = need_u32(r, "protocolVersion"); n.ok()) v.protocol_version = *n;
  return v;
}

Result<FeeStats> RpcClient::get_fee_stats() {
  STELLAR_TRY_ASSIGN(auto r, call("getFeeStats", Json::object()));
  // RPC returns fee values as strings ("100") on some deployments, numbers on others
  auto num = [](const Json& j) -> std::uint64_t {
    if (j.is_number()) return static_cast<std::uint64_t>(j.as_int());
    if (j.is_string()) {
      try {
        return std::stoull(j.as_string());
      } catch (...) {
      }
    }
    return 0;
  };
  auto bucket = [&](const Json& s) {
    FeeBucket b;
    b.min = num(s.at("min"));
    b.mode = num(s.at("mode"));
    b.p10 = num(s.at("p10"));
    b.p50 = num(s.at("p50"));
    b.p90 = num(s.at("p90"));
    b.p95 = num(s.at("p95"));
    b.p99 = num(s.at("p99"));
    b.max = num(s.at("max"));
    b.transaction_count = num(s.at("transactionCount"));
    b.ledger_count = num(s.at("ledgerCount"));
    return b;
  };
  FeeStats f;
  if (const auto* s = r.find("sorobanInclusionFee")) f.soroban_inclusion = bucket(*s);
  if (const auto* s = r.find("inclusionFee")) f.inclusion = bucket(*s);
  return f;
}

Result<Json> RpcClient::get_network() {
  return call("getNetwork", Json::object());
}

// ---------------------------------------------------------------------------
// ledger entries / ledgers / events
// ---------------------------------------------------------------------------

Result<LedgerEntriesResponse> RpcClient::get_ledger_entries(
    const std::vector<xdr::LedgerKey>& keys) {
  LedgerEntriesResponse out;
  // RPC cap: max 200 keys per request — chunk automatically
  for (std::size_t off = 0; off < keys.size(); off += 200) {
    Json::Array jkeys;
    const auto end = std::min(off + 200, keys.size());
    for (std::size_t i = off; i < end; ++i) {
      STELLAR_TRY_ASSIGN(auto b, xdr::encode(keys[i]));
      jkeys.push_back(base64_encode(ByteSpan(b.data(), b.size())));
    }
    Json params = Json::object();
    params.set("keys", Json(std::move(jkeys)));
    STELLAR_TRY_ASSIGN(auto r, call("getLedgerEntries", std::move(params)));
    STELLAR_TRY_ASSIGN(auto chunk, parse_ledger_entries(r));
    out.entries.insert(out.entries.end(), std::make_move_iterator(chunk.entries.begin()),
                       std::make_move_iterator(chunk.entries.end()));
    out.latest_ledger = chunk.latest_ledger;
  }
  return out;
}

Result<LedgerEntriesResponse> RpcClient::parse_ledger_entries(const Json& r) {
  LedgerEntriesResponse out;
  if (auto n = need_u32(r, "latestLedger"); n.ok()) out.latest_ledger = *n;
  for (const auto& e : safe_array(r.at("entries"))) {
    LedgerEntryRow row;
    // deployments differ: some emit "keyXdr", newer ones emit "key"
    if (e.at("keyXdr").is_string()) {
      STELLAR_TRY_ASSIGN(row.key, xdr_field<xdr::LedgerKey>(e, "keyXdr"));
    } else {
      STELLAR_TRY_ASSIGN(row.key, xdr_field<xdr::LedgerKey>(e, "key"));
    }
    // lazy decode: keep raw + error on failure instead of failing the call —
    // one unfamiliar arm after a protocol upgrade shouldn't sink the response
    const auto& raw = e.at("xdr").as_string();
    auto entry_bytes = base64_decode(raw);
    if (!entry_bytes.ok()) {
      row.raw_xdr = raw;
      row.decode_error = entry_bytes.error();
    } else {
      auto entry = xdr::decode<xdr::LedgerEntry>(*entry_bytes);
      if (entry.ok()) {
        row.data = entry->data;
        row.last_modified_ledger = static_cast<std::uint32_t>(entry->lastModifiedLedgerSeq);
      } else {
        auto d = xdr::decode<xdr::LedgerEntryData>(*entry_bytes);
        if (d.ok()) {
          row.data = *d;
        } else {
          row.raw_xdr = raw;
          row.decode_error = d.error();
        }
      }
    }
    if (auto n = need_u32(e, "lastModifiedLedgerSeq"); n.ok()) row.last_modified_ledger = *n;
    if (const auto* lt = e.find("liveUntilLedgerSeq"); lt && lt->is_number())
      row.live_until_ledger = static_cast<std::uint32_t>(lt->as_int());
    out.entries.push_back(std::move(row));
  }
  return out;
}

Result<LedgersResponse> RpcClient::get_ledgers(std::uint32_t start, std::uint32_t limit) {
  Json params = Json::object();
  params.set("startLedger", Json(static_cast<std::int64_t>(start)));
  params.set("pagination", Json(Json::Object{{"limit", Json(static_cast<std::int64_t>(limit))}}));
  STELLAR_TRY_ASSIGN(auto r, call("getLedgers", std::move(params)));
  LedgersResponse out;
  if (auto n = need_u32(r, "latestLedger"); n.ok()) out.latest_ledger = *n;
  if (const auto* c = r.find("cursor"); c && c->is_string())
    out.cursor = std::stoull(c->as_string());
  for (const auto& l : safe_array(r.at("ledgers"))) {
    LedgerRow row;
    row.sequence = static_cast<std::uint32_t>(l.at("sequence").as_int());
    if (l.at("hash").is_string()) row.hash = l.at("hash").as_string();
    if (l.at("ledgerCloseTime").is_number())
      row.close_time = static_cast<std::uint64_t>(l.at("ledgerCloseTime").as_int());
    STELLAR_TRY_ASSIGN(row.meta, xdr_field<xdr::LedgerCloseMeta>(l, "metadataXdr"));
    out.ledgers.push_back(std::move(row));
  }
  return out;
}

Result<EventsResponse> RpcClient::get_events(std::uint32_t start_ledger,
                                             std::vector<EventsFilter> filters) {
  Json params = Json::object();
  params.set("startLedger", Json(static_cast<std::int64_t>(start_ledger)));
  Json::Array jfilters;
  std::uint32_t limit = 50;
  for (const auto& filter : filters) {
    Json::Object filt;
    if (filter.type) filt.emplace("type", *filter.type);
    if (!filter.contract_ids.empty()) {
      Json::Array ids;
      for (const auto& c : filter.contract_ids) ids.push_back(Json(c));
      filt.emplace("contractIds", Json(std::move(ids)));
    }
    if (!filter.topic.empty()) {
      Json::Array topics;
      for (const auto& t : filter.topic) {
        Json::Array seg;
        for (const auto& scv : t) {
          STELLAR_TRY_ASSIGN(auto b, xdr::encode(scv));
          seg.push_back(base64_encode(ByteSpan(b.data(), b.size())));
        }
        topics.push_back(Json(std::move(seg)));
      }
      filt.emplace("topics", Json(std::move(topics)));
    }
    limit = filter.limit;
    jfilters.push_back(Json(std::move(filt)));
  }
  Json pagination = Json::object();
  pagination.set("limit", Json(static_cast<std::int64_t>(limit)));
  params.set("filters", Json(std::move(jfilters)));
  params.set("pagination", std::move(pagination));
  STELLAR_TRY_ASSIGN(auto r, call("getEvents", std::move(params)));

  EventsResponse out;
  if (auto n = need_u32(r, "latestLedger"); n.ok()) out.latest_ledger = *n;
  if (const auto* c = r.find("cursor"); c && c->is_string()) out.cursor = c->as_string();
  for (const auto& e : safe_array(r.at("events"))) {
    EventRow row;
    if (e.at("type").is_string()) row.type = e.at("type").as_string();
    row.ledger = static_cast<std::uint32_t>(e.at("ledger").as_int());
    if (e.at("ledgerClosedAt").is_string())
      row.ledger_closed_at = e.at("ledgerClosedAt").as_string();
    if (e.at("contractId").is_string()) row.contract_id = e.at("contractId").as_string();
    if (e.at("id").is_string()) row.id = e.at("id").as_string();
    if (e.at("txHash").is_string()) row.tx_hash = e.at("txHash").as_string();
    row.in_successful_contract_call =
        e.at("inSuccessfulContractCall").is_bool() && e.at("inSuccessfulContractCall").as_bool();
    for (const auto& t : safe_array(e.at("topic"))) {
      if (t.is_string()) {
        STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::SCVal>(t.as_string()));
        row.topic.push_back(std::move(v));
      }
    }
    if (e.at("value").is_string()) {
      STELLAR_TRY_ASSIGN(row.value, xdr_b64<xdr::SCVal>(e.at("value").as_string()));
    }
    out.events.push_back(std::move(row));
  }
  return out;
}

// ---------------------------------------------------------------------------
// transactions
// ---------------------------------------------------------------------------

Result<TransactionResponse> RpcClient::get_transaction(std::string_view hash) {
  Json params = Json::object();
  params.set("hash", std::string(hash));
  STELLAR_TRY_ASSIGN(auto r, call("getTransaction", std::move(params)));
  TransactionResponse out;
  if (r.at("status").is_string()) out.status = r.at("status").as_string();
  out.found = out.status == "SUCCESS" || out.status == "FAILED";
  if (r.at("ledger").is_number()) out.ledger = static_cast<std::uint32_t>(r.at("ledger").as_int());
  if (r.at("applicationOrder").is_number())
    out.application_order = static_cast<std::uint32_t>(r.at("applicationOrder").as_int());
  if (r.at("ledgerCloseTime").is_number())
    out.ledger_closed_at = std::to_string(r.at("ledgerCloseTime").as_int());
  if (r.at("txHash").is_string()) out.tx_hash = r.at("txHash").as_string();
  if (r.at("resultXdr").is_string()) {
    STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::TransactionResult>(r.at("resultXdr").as_string()));
    out.result = std::move(v);
  }
  if (r.at("resultMetaXdr").is_string()) {
    STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::TransactionMeta>(r.at("resultMetaXdr").as_string()));
    out.meta = std::move(v);
  }
  if (r.at("envelopeXdr").is_string()) {
    STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::TransactionEnvelope>(r.at("envelopeXdr").as_string()));
    out.envelope = std::move(v);
  }
  return out;
}

Result<SendTransactionResponse> RpcClient::send_transaction(const TransactionEnvelope& envelope) {
  STELLAR_TRY_ASSIGN(auto b64, envelope.to_base64());
  Json params = Json::object();
  params.set("transaction", std::move(b64));
  STELLAR_TRY_ASSIGN(auto r, call("sendTransaction", std::move(params)));
  SendTransactionResponse out;
  if (r.at("status").is_string()) out.status = r.at("status").as_string();
  if (r.at("hash").is_string()) out.hash = r.at("hash").as_string();
  if (r.at("errorResultXdr").is_string()) {
    STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::TransactionResult>(r.at("errorResultXdr").as_string()));
    out.error_result = std::move(v);
  }
  for (const auto& d : safe_array(r.at("diagnosticEventsXdr"))) {
    if (d.is_string()) out.diagnostic_events_xdr.push_back(d.as_string());
  }
  return out;
}

Result<SimulateTransactionResponse> RpcClient::simulate_transaction(const xdr::Transaction& tx) {
  // simulateTransaction expects a TransactionEnvelope; an unsigned envelope
  // (empty signatures) is fine for simulation.
  return simulate_envelope(TransactionEnvelope::of_transaction(tx));
}

Result<SimulateTransactionResponse> RpcClient::simulate_envelope(
    const TransactionEnvelope& envelope) {
  STELLAR_TRY_ASSIGN(auto bytes, envelope.to_bytes());
  Json params = Json::object();
  params.set("transaction", base64_encode(ByteSpan(bytes.data(), bytes.size())));
  return simulate_envelope_params(std::move(params));
}

Result<SimulateTransactionResponse> RpcClient::simulate_envelope_params(Json params) {
  STELLAR_TRY_ASSIGN(auto r, call("simulateTransaction", std::move(params)));
  SimulateTransactionResponse out;
  if (const auto* e = r.find("error"); e && e->is_string()) out.error = e->as_string();
  if (const auto* f = r.find("minResourceFee"); f && f->is_string())
    out.min_resource_fee = std::stoll(f->as_string());
  if (r.at("resultXdr").is_string()) {
    STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::TransactionResult>(r.at("resultXdr").as_string()));
    out.result = std::move(v);
  }
  // field renamed over time: current "transactionData", legacy "transactionDataXdr"
  const auto* td = r.at("transactionData").is_string()      ? &r.at("transactionData")
                   : r.at("transactionDataXdr").is_string() ? &r.at("transactionDataXdr")
                                                            : nullptr;
  if (td) {
    STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::SorobanTransactionData>(td->as_string()));
    out.transaction_data = std::move(v);
  }
  if (const auto* m = r.find("minResourceFee")) {
    if (m->is_string())
      out.min_resource_fee = std::stoll(m->as_string());
    else if (m->is_number())
      out.min_resource_fee = m->as_int();
  }
  for (const auto& res : safe_array(r.at("results"))) {
    if (res.at("xdr").is_string()) {
      STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::SCVal>(res.at("xdr").as_string()));
      out.results.push_back(std::move(v));
    }
    // auth entries are nested inside each invocation's result
    for (const auto& a : safe_array(res.at("auth"))) {
      if (a.is_string()) {
        STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::SorobanAuthorizationEntry>(a.as_string()));
        out.auth.push_back(std::move(v));
      }
    }
  }
  // some older responses also carry a top-level array — accept both
  for (const auto& a : safe_array(r.at("authXdr"))) {
    if (a.is_string()) {
      STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::SorobanAuthorizationEntry>(a.as_string()));
      out.auth.push_back(std::move(v));
    }
  }
  for (const auto& d : safe_array(r.at("eventsXdr"))) {
    if (d.is_string()) out.events_xdr.push_back(d.as_string());
  }
  for (const auto& d : safe_array(r.at("events"))) {
    if (d.is_string()) out.events_xdr.push_back(d.as_string());
  }
  if (const auto* rp = r.find("restorePreamble"); rp && rp->is_object()) {
    SimulateTransactionResponse::RestorePreamble pre;
    if (rp->at("transactionData").is_string()) {
      STELLAR_TRY_ASSIGN(
          auto v, xdr_b64<xdr::SorobanTransactionData>(rp->at("transactionData").as_string()));
      pre.transaction_data = std::move(v);
    }
    if (rp->at("minResourceFee").is_string())
      pre.min_resource_fee = std::stoll(rp->at("minResourceFee").as_string());
    out.restore_preamble = std::move(pre);
  }
  return out;
}

Result<TransactionResponse> RpcClient::send_and_poll(const TransactionEnvelope& envelope,
                                                     int max_polls,
                                                     std::uint32_t poll_interval_ms) {
  SendTransactionResponse sent;
  for (int attempt = 0;; ++attempt) {
    STELLAR_TRY_ASSIGN(sent, send_transaction(envelope));
    if (sent.status != "TRY_AGAIN_LATER" || attempt >= 10) break;
    // server says retry: linear backoff
    std::this_thread::sleep_for(
        std::chrono::milliseconds(500u * static_cast<std::uint32_t>(attempt + 1)));
  }
  if (sent.status == "DUPLICATE") {
    // a resubmitted tx the server already knows — keep polling for its result
    TransactionResponse dup;
    dup.status = "DUPLICATE";
    dup.found = true;
    return dup;
  }
  if (sent.status == "ERROR" || sent.status.empty()) {
    TransactionResponse err;
    err.status = "ERROR";
    if (sent.error_result) err.result = *sent.error_result;
    return err;
  }
  for (int i = 0; i < max_polls; ++i) {
    if (i > 0) std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    auto res = get_transaction(sent.hash);
    if (res.ok() && res->found && (res->status == "SUCCESS" || res->status == "FAILED")) {
      return res;
    }
  }
  return Error{ErrorCode::kRpcError, "transaction status poll timed out"};
}

namespace {
/// Instance SCVal -> wasm hash (CONTRACT_EXECUTABLE_WASM arm); else nullptr.
const xdr::Hash* scval_get_contract_instance(const xdr::SCVal& v) {
  if (v.type() != xdr::SCValType::SCV_CONTRACT_INSTANCE) return nullptr;
  const auto& e = v.instance().get().executable;
  return e.type() == xdr::ContractExecutableType::CONTRACT_EXECUTABLE_WASM ? &e.wasm_hash()
                                                                           : nullptr;
}
}  // namespace

Result<SimulateTransactionResponse> RpcClient::simulate(const TransactionEnvelope& envelope,
                                                        const SimulateOptions& opts) {
  STELLAR_TRY_ASSIGN(auto bytes, envelope.to_bytes());
  Json params = Json::object();
  params.set("transaction", base64_encode(ByteSpan(bytes.data(), bytes.size())));
  if (opts.instruction_leeway) {
    Json rc = Json::object();
    rc.set("instructionLeeway", Json(static_cast<std::int64_t>(*opts.instruction_leeway)));
    params.set("resourceConfig", std::move(rc));
  }
  if (opts.auth_mode) params.set("authMode", *opts.auth_mode);
  if (opts.use_upgraded_auth) params.set("useUpgradedAuth", true);
  if (opts.xdr_format_json) params.set("xdrFormat", "json");
  return simulate_envelope_params(std::move(params));
}

Result<std::vector<TransactionResponse>> RpcClient::get_transactions(
    const std::vector<std::string>& tx_hashes) {
  std::vector<TransactionResponse> out;
  for (std::size_t off = 0; off < tx_hashes.size(); off += 50) {
    Json::Array j;
    const auto end = std::min(off + 50, tx_hashes.size());
    for (std::size_t i = off; i < end; ++i) j.push_back(Json(tx_hashes[i]));
    Json params = Json::object();
    params.set("hashes", Json(std::move(j)));
    STELLAR_TRY_ASSIGN(auto r, call("getTransactions", std::move(params)));
    for (const auto& t : safe_array(r.at("transactions"))) {
      TransactionResponse row;
      if (t.at("status").is_string()) row.status = t.at("status").as_string();
      row.found = row.status != "NOT_FOUND";
      row.ledger = static_cast<std::uint32_t>(t.at("ledger").as_int());
      if (t.at("txHash").is_string()) row.tx_hash = t.at("txHash").as_string();
      if (t.at("resultXdr").is_string()) {
        STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::TransactionResult>(t.at("resultXdr").as_string()));
        row.result = std::move(v);
      }
      if (t.at("envelopeXdr").is_string()) {
        STELLAR_TRY_ASSIGN(auto v,
                           xdr_b64<xdr::TransactionEnvelope>(t.at("envelopeXdr").as_string()));
        row.envelope = std::move(v);
      }
      out.push_back(std::move(row));
    }
  }
  return out;
}

Result<EventsResponse> RpcClient::query_events(std::uint32_t start_ledger,
                                               std::vector<EventsFilter> filters) {
  // beta: same wire shape as getEvents under the queryEvents method name
  Json params = Json::object();
  params.set("startLedger", Json(static_cast<std::int64_t>(start_ledger)));
  Json::Array jfilters;
  for (const auto& f : filters) {
    Json::Object fo;
    if (f.type) fo.emplace("type", *f.type);
    jfilters.push_back(Json(std::move(fo)));
  }
  params.set("filters", Json(std::move(jfilters)));
  STELLAR_TRY_ASSIGN(auto r, call("queryEvents", std::move(params)));
  EventsResponse out;
  for (const auto& e : safe_array(r.at("events"))) {
    EventRow ev;
    if (e.at("type").is_string()) ev.type = e.at("type").as_string();
    ev.ledger = static_cast<std::uint32_t>(e.at("ledger").as_int());
    ev.contract_id = e.at("contractId").as_string();
    if (e.at("value").is_string()) {
      STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::SCVal>(e.at("value").as_string()));
      ev.value = std::move(v);
    }
    for (const auto& t : safe_array(e.at("topic"))) {
      STELLAR_TRY_ASSIGN(auto v, xdr_b64<xdr::SCVal>(t.as_string()));
      ev.topic.push_back(std::move(v));
    }
    out.events.push_back(std::move(ev));
  }
  return out;
}

Result<xdr::AccountEntry> RpcClient::get_account(const Address& account) {
  STELLAR_TRY_ASSIGN(auto mx, account.to_muxed_account());
  xdr::uint256 pk{};
  if (mx.type() == xdr::CryptoKeyType::KEY_TYPE_MUXED_ED25519)
    pk = mx.med25519().ed25519;
  else
    pk = mx.ed25519();
  xdr::LedgerKey key =
      xdr::LedgerKey::account(xdr::LedgerKeyAccount{xdr::PublicKey::publicKeyTypeEd25519(pk)});
  STELLAR_TRY_ASSIGN(auto res, get_ledger_entries({key}));
  if (res.entries.empty() || res.entries[0].data.type() != xdr::LedgerEntryType::ACCOUNT) {
    return Error{ErrorCode::kProtocolError, "account not found"};
  }
  return res.entries[0].data.account();
}

Result<xdr::LedgerEntryData> RpcClient::get_contract_data(std::string_view contract_id,
                                                          const xdr::SCVal& key) {
  auto cid = StrKey::decode(contract_id);
  if (!cid.ok() || cid->kind() != StrKeyKind::kContract) {
    return Error::invalid_argument("expected a C... contract id");
  }
  xdr::LedgerKey lk = xdr::LedgerKey::contractData(xdr::LedgerKeyContractData{
      xdr::SCAddress::scAddressTypeContract(xdr::ContractID{*cid->fixed()}), key,
      xdr::ContractDataDurability::PERSISTENT});
  STELLAR_TRY_ASSIGN(auto res, get_ledger_entries({lk}));
  if (res.entries.empty()) {
    return Error{ErrorCode::kProtocolError, "contract data not found"};
  }
  return res.entries[0].data;
}

Result<Bytes> RpcClient::get_contract_code(std::string_view contract_id) {
  auto cid = StrKey::decode(contract_id);
  if (!cid.ok() || cid->kind() != StrKeyKind::kContract) {
    return Error::invalid_argument("expected a C... contract id");
  }
  // two-step: instance entry -> wasm executable hash -> contractCode entry
  xdr::SCAddress scaddr = xdr::SCAddress::scAddressTypeContract(xdr::ContractID{*cid->fixed()});
  xdr::LedgerKey instance_key = xdr::LedgerKey::contractData(xdr::LedgerKeyContractData{
      scaddr, xdr::SCVal::scvLedgerKeyContractInstance(), xdr::ContractDataDurability::PERSISTENT});
  STELLAR_TRY_ASSIGN(auto inst, get_ledger_entries({instance_key}));
  if (inst.entries.empty() || inst.entries[0].data.type() != xdr::LedgerEntryType::CONTRACT_DATA) {
    return Error{ErrorCode::kProtocolError, "contract instance not found"};
  }
  const auto* cd = inst.entries[0].data.get_if_contractData();
  if (!cd) return Error{ErrorCode::kProtocolError, "entry is not contract data"};
  const auto* inst_val = scval_get_contract_instance(cd->val);
  if (!inst_val) {
    return Error{ErrorCode::kProtocolError, "instance val has no executable"};
  }
  xdr::LedgerKey code_key = xdr::LedgerKey::contractCode(xdr::LedgerKeyContractCode{*inst_val});
  STELLAR_TRY_ASSIGN(auto res, get_ledger_entries({code_key}));
  if (res.entries.empty()) {
    return Error{ErrorCode::kProtocolError, "contract code not found"};
  }
  const auto& d = res.entries[0].data;
  if (d.type() != xdr::LedgerEntryType::CONTRACT_CODE) {
    return Error{ErrorCode::kProtocolError, "entry is not contract code"};
  }
  const auto& code = d.contractCode();
  return Bytes(code.code.begin(), code.code.end());
}

Result<Json> RpcClient::get_json(std::string_view url) {
  HttpRequest req;
  req.method = "GET";
  req.url = std::string(url);
  STELLAR_TRY_ASSIGN(auto res, transport_.perform(req));
  if (res.status != 200) {
    return Error{ErrorCode::kHttpError, "HTTP " + std::to_string(res.status)};
  }
  return Json::parse(res.body);
}

Result<Json> RpcClient::friendbot_fund(std::string_view friendbot_url, const Address& account) {
  const std::string url = std::string(friendbot_url) + "?addr=" + account.to_string();
  return get_json(url);
}

}  // namespace stellar::inline v1
