// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/error.hpp"

namespace stellar::inline v1 {

std::string_view error_code_name(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk:
      return "ok";
    case ErrorCode::kInvalidArgument:
      return "invalid_argument";
    case ErrorCode::kDecodeFailed:
      return "decode_failed";
    case ErrorCode::kEncodeFailed:
      return "encode_failed";
    case ErrorCode::kCryptoFailed:
      return "crypto_failed";
    case ErrorCode::kInvalidStrKey:
      return "invalid_strkey";
    case ErrorCode::kInvalidAddress:
      return "invalid_address";
    case ErrorCode::kNetwork:
      return "network";
    case ErrorCode::kTimeout:
      return "timeout";
    case ErrorCode::kRateLimited:
      return "rate_limited";
    case ErrorCode::kHttpError:
      return "http_error";
    case ErrorCode::kRpcError:
      return "rpc_error";
    case ErrorCode::kHorizonError:
      return "horizon_error";
    case ErrorCode::kTransactionFailed:
      return "transaction_failed";
    case ErrorCode::kAuthError:
      return "auth_error";
    case ErrorCode::kProtocolError:
      return "protocol_error";
    case ErrorCode::kOutOfRange:
      return "out_of_range";
    case ErrorCode::kNotSupported:
      return "not_supported";
    case ErrorCode::kInternal:
      return "internal";
  }
  return "unknown";
}

std::string Error::describe() const {
  std::string out;
  for (const auto& layer : context_) {
    out += layer;
    out += ": ";
  }
  out += message_;
  return out;
}

}  // namespace stellar::inline v1
