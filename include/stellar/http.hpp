// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file http.hpp
/// Pluggable synchronous HTTP transport. `HttpTransport` is the seam used by
/// the RPC and Horizon clients; `CurlTransport` is the default on desktop.

#include "stellar/export.hpp"
#include "stellar/result.hpp"
#include "stellar/util/bytes.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1 {

struct STELLAR_API HttpRequest {
  std::string method{"POST"};
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
  std::uint32_t timeout_ms{30'000};
};

struct STELLAR_API HttpResponse {
  long status{0};  // HTTP status; 0 => transport-level failure
  std::string body;
  std::map<std::string, std::string> headers;
};

/// Cooperative cancellation for in-flight transfers and polls.
class STELLAR_API CancelToken {
 public:
  void cancel() noexcept { cancelled_.store(true); }
  [[nodiscard]] bool cancelled() const noexcept { return cancelled_.load(); }

 private:
  std::atomic<bool> cancelled_{false};
};

/// Streaming sink: receives response headers once, then data chunks.
/// Returning false from on_data aborts the transfer.
class STELLAR_API StreamSink {
 public:
  virtual ~StreamSink() = default;
  virtual void on_headers(long /*status*/, const std::map<std::string, std::string>& /*headers*/) {}
  [[nodiscard]] virtual bool on_data(const void* data, std::size_t size) = 0;
};

/// A synchronous, thread-safe HTTP transport.
class STELLAR_API HttpTransport {
 public:
  virtual ~HttpTransport() = default;
  virtual Result<HttpResponse> perform(const HttpRequest& request) = 0;
  /// Streaming variant; the default falls back to perform() and delivers one
  /// chunk. Transports may honor `cancel` mid-transfer.
  virtual Result<long> stream(const HttpRequest& request, StreamSink& sink,
                              CancelToken* cancel = nullptr);
};

/// Decorator adding retries (backoff + jitter, honors Retry-After and 429/5xx),
/// X-Client-Name/Version identity headers, and an optional metrics hook.
class STELLAR_API RetryingTransport final : public HttpTransport {
 public:
  explicit RetryingTransport(HttpTransport& inner, int max_attempts = 3,
                             std::uint32_t base_delay_ms = 200)
      : inner_(inner), max_attempts_(max_attempts), base_delay_ms_(base_delay_ms) {}

  void set_client_name(std::string name) { client_name_ = std::move(name); }
  void set_client_version(std::string v) { client_version_ = std::move(v); }
  /// Called after each attempt: (status, attempt elapsed_ms). May be nullptr.
  void set_metrics(std::function<void(long, std::uint32_t)> cb) { metrics_ = std::move(cb); }

  Result<HttpResponse> perform(const HttpRequest& request) override;
  Result<long> stream(const HttpRequest& request, StreamSink& sink,
                      CancelToken* cancel = nullptr) override {
    return inner_.stream(request, sink, cancel);  // streaming passes through
  }

 private:
  HttpTransport& inner_;
  int max_attempts_;
  std::uint32_t base_delay_ms_;
  std::string client_name_{"stellar-cpp-sdk"};
  std::string client_version_{"0.1.0"};
  std::function<void(long, std::uint32_t)> metrics_;
};

/// libcurl-based transport with TLS verification enabled by default.
/// Construct once, share across clients; perform() is thread-safe.
class STELLAR_API CurlTransport final : public HttpTransport {
 public:
  CurlTransport();
  ~CurlTransport() override;
  CurlTransport(const CurlTransport&) = delete;
  CurlTransport& operator=(const CurlTransport&) = delete;

  /// Disable TLS certificate verification. FOR TESTING ONLY — never in
  /// production code paths.
  void set_tls_verify(bool verify) { tls_verify_ = verify; }
  void set_user_agent(std::string ua) { user_agent_ = std::move(ua); }

  Result<HttpResponse> perform(const HttpRequest& request) override;
  /// Incremental delivery: on_headers once, then on_data chunks. Honors
  /// `cancel` and on_data's false return mid-transfer.
  Result<long> stream(const HttpRequest& request, StreamSink& sink,
                      CancelToken* cancel = nullptr) override;

 private:
  bool tls_verify_ = true;
  std::string user_agent_ = "stellar-cpp-sdk/0.1";
};

/// RFC 7578 multipart/form-data encoder (file uploads, e.g. Horizon toml
/// uploads). Produces `Content-Type: multipart/form-data; boundary=...` and the
/// encoded body.
class STELLAR_API MultipartEncoder {
 public:
  MultipartEncoder() = default;
  /// Add a text field.
  void add_field(std::string_view name, std::string_view value);
  /// Add a file part (binary or text) with a filename.
  void add_file(std::string_view name, std::string_view filename, ByteSpan content,
                std::string_view mime = "application/octet-stream");
  /// Content-Type header value.
  [[nodiscard]] std::string content_type() const {
    return "multipart/form-data; boundary=" + boundary_;
  }
  /// Encoded body.
  [[nodiscard]] const std::string& body() const { return body_; }

 private:
  std::string boundary_ = "----stellar-sdk-boundary";
  std::string body_;
};

/// Server-sent events stream parser — Horizon/RPC streaming uses `data:`/
/// `event:`/`id:`/`retry:` lines; yields one EventStreamFrame per blank line.
struct STELLAR_API EventStreamFrame {
  std::string event;  // "message" if unset
  std::string data;   // accumulated `data:` payload
  std::string id;
  std::string retry;  // ms
};

/// Incremental SSE decoder: feed chunks, poll complete frames.
/// Thread-safe only per-instance.
class STELLAR_API SseParser {
 public:
  /// Feed raw bytes; returns frames completed since last call.
  [[nodiscard]] std::vector<EventStreamFrame> feed(std::string_view chunk);

 private:
  std::string buf_;
  EventStreamFrame cur_;
  bool in_frame_ = false;
};

/// In-memory transport for tests — registers canned responses per URL/method
/// pair and records every request for assertion. Shipped publicly so users
/// can test their own code paths over the same seam.
class STELLAR_API MockTransport final : public HttpTransport {
 public:
  /// Register a canned response for a URL substring match.
  void add_response(std::string_view url_substring, HttpResponse response) {
    canned_.emplace_back(std::string(url_substring), std::move(response));
  }
  /// Register a canned error for a URL substring.
  void add_error(std::string_view url_substring, Error error) {
    errors_.emplace_back(std::string(url_substring), std::move(error));
  }
  /// Every request performed, in order (assert on method/headers/body).
  [[nodiscard]] const std::vector<HttpRequest>& requests() const { return requests_; }

  Result<HttpResponse> perform(const HttpRequest& request) override {
    requests_.push_back(request);
    for (const auto& [sub, err] : errors_) {
      if (request.url.find(sub) != std::string::npos) return err;
    }
    for (const auto& [sub, res] : canned_) {
      if (request.url.find(sub) != std::string::npos) return res;
    }
    return Error{ErrorCode::kNetwork, "MockTransport: no response registered for " + request.url};
  }

 private:
  std::vector<std::pair<std::string, HttpResponse>> canned_;
  std::vector<std::pair<std::string, Error>> errors_;
  std::vector<HttpRequest> requests_;
};

}  // namespace stellar::inline v1
