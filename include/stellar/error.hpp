// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file error.hpp
/// Error type carried by stellar::Result.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stellar::inline v1 {

/// Broad failure categories. Protocol- and transport-specific detail travels
/// in Error::detail rather than multiplying the enum.
enum class ErrorCode : std::int32_t {
  kOk = 0,
  kInvalidArgument,
  kDecodeFailed,      ///< XDR decode: malformed input, bound/depth exceeded, non-canonical.
  kEncodeFailed,      ///< XDR encode: value violates a bound or has an invalid discriminant.
  kCryptoFailed,      ///< Key generation, signing or verification failed.
  kInvalidStrKey,     ///< SEP-23 strkey rejected (bad checksum, length, padding or version).
  kInvalidAddress,    ///< Address/memo/asset/mnemonic rejected by validation.
  kNetwork,           ///< Transport-level failure (DNS, connect, TLS, read/write).
  kTimeout,
  kRateLimited,       ///< HTTP 429.
  kHttpError,         ///< Any other non-2xx HTTP status.
  kRpcError,          ///< JSON-RPC error object.
  kHorizonError,      ///< Horizon problem+json response.
  kTransactionFailed, ///< Submitted transaction failed; detail carries result codes.
  kAuthError,         ///< SEP-10/SEP-45 challenge or signing failure.
  kProtocolError,     ///< Peer/server sent a structurally invalid response.
  kOutOfRange,
  kNotSupported,
  kInternal,          ///< SDK bug or violated internal invariant.
};

std::string_view error_code_name(ErrorCode code) noexcept;

/// Machine-readable detail attached to domain errors. Optional per layer.
struct ErrorDetail {
  int http_status = 0;                       ///< HTTP status code, when relevant.
  std::int64_t rpc_code = 0;                 ///< JSON-RPC error.code, when relevant.
  std::vector<std::string> result_codes;     ///< Stellar tx/operation result codes (e.g. "tx_failed", "op_no_trust").
  bool operator==(const ErrorDetail&) const = default;
};

/// A structured error. Message text always names the failing operation;
/// context() entries are outer-to-inner layers added by `with_context`/`push_context`.
class Error {
 public:
  Error() = default;
  Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

  static Error invalid_argument(std::string message) { return {ErrorCode::kInvalidArgument, std::move(message)}; }
  static Error decode_failed(std::string message) { return {ErrorCode::kDecodeFailed, std::move(message)}; }
  static Error encode_failed(std::string message) { return {ErrorCode::kEncodeFailed, std::move(message)}; }
  static Error network(std::string message) { return {ErrorCode::kNetwork, std::move(message)}; }
  static Error timeout(std::string message) { return {ErrorCode::kTimeout, std::move(message)}; }
  static Error internal(std::string message) { return {ErrorCode::kInternal, std::move(message)}; }

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] const std::vector<std::string>& context() const noexcept { return context_; }
  [[nodiscard]] const ErrorDetail& detail() const noexcept { return detail_; }
  [[nodiscard]] ErrorDetail& mutable_detail() noexcept { return detail_; }

  /// Returns a copy with an outer context layer (e.g. "fetching challenge").
  [[nodiscard]] Error with_context(std::string layer) && {
    context_.insert(context_.begin(), std::move(layer));
    return std::move(*this);
  }

  /// "layer1: layer2: message"
  [[nodiscard]] std::string describe() const;

  bool operator==(const Error&) const = default;

 private:
  ErrorCode code_ = ErrorCode::kInternal;
  std::string message_;
  std::vector<std::string> context_;
  ErrorDetail detail_;
};

}  // namespace stellar::inline v1
