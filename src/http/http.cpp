// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/http.hpp"

#include <chrono>
#include <random>
#include <thread>

namespace stellar::inline v1 {

Result<long> HttpTransport::stream(const HttpRequest& request, StreamSink& sink,
                                   CancelToken* cancel) {
  STELLAR_TRY_ASSIGN(auto res, perform(request));
  if (cancel && cancel->cancelled()) {
    return Error{ErrorCode::kInvalidArgument, "cancelled"};
  }
  sink.on_headers(res.status, res.headers);
  sink.on_data(res.body.data(), res.body.size());
  return res.status;
}

namespace {

std::uint32_t retry_after_ms(const HttpResponse& res) {
  const auto it = res.headers.find("Retry-After");
  if (it == res.headers.end()) {
    const auto it2 = res.headers.find("retry-after");
    if (it2 == res.headers.end()) return 0;
    return static_cast<std::uint32_t>(std::stoul(it2->second)) * 1000;
  }
  return static_cast<std::uint32_t>(std::stoul(it->second)) * 1000;
}

bool retryable(long status) {
  return status == 0 || status == 429 || status == 500 || status == 502 ||
         status == 503 || status == 504;
}

}  // namespace

Result<HttpResponse> RetryingTransport::perform(const HttpRequest& request) {
  std::mt19937 rng(std::random_device{}());
  std::uniform_real_distribution<double> jitter(0.5, 1.5);

  HttpRequest req = request;
  req.headers["X-Client-Name"] = client_name_;
  req.headers["X-Client-Version"] = client_version_;

  for (int attempt = 0;; ++attempt) {
    const auto t0 = std::chrono::steady_clock::now();
    auto res = inner_.perform(req);
    const auto ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count());
    if (!res.ok()) {
      if (metrics_) metrics_(0, ms);
      return res;  // transport error -> surface immediately
    }
    if (metrics_) metrics_(res->status, ms);
    if (!retryable(res->status) || attempt + 1 >= max_attempts_) return res;

    // backoff: Retry-After wins, else exponential with jitter
    std::uint32_t wait = retry_after_ms(*res);
    if (wait == 0) {
      wait = static_cast<std::uint32_t>(
          base_delay_ms_ * (1u << attempt) * jitter(rng));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(wait));
  }
}




void MultipartEncoder::add_field(std::string_view name, std::string_view value) {
  body_ += "--" + boundary_ + "\r\nContent-Disposition: form-data; name=\"" +
           std::string(name) + "\"\r\n\r\n" + std::string(value) + "\r\n";
}
void MultipartEncoder::add_file(std::string_view name, std::string_view filename,
                                ByteSpan content, std::string_view mime) {
  body_ += "--" + boundary_ +
           "\r\nContent-Disposition: form-data; name=\"" + std::string(name) +
           "\"; filename=\"" + std::string(filename) +
           "\"\r\nContent-Type: " + std::string(mime) + "\r\n\r\n";
  body_.append(reinterpret_cast<const char*>(content.data()), content.size());
  body_ += "\r\n";
}

std::vector<EventStreamFrame> SseParser::feed(std::string_view chunk) {
  std::vector<EventStreamFrame> out;
  buf_.append(chunk.data(), chunk.size());
  std::size_t pos;
  while ((pos = buf_.find('\n')) != std::string::npos) {
    std::string line = buf_.substr(0, pos);
    buf_.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) {  // blank line = frame boundary
      if (in_frame_) {
        out.push_back(cur_);
        cur_ = EventStreamFrame{};
        in_frame_ = false;
      }
      continue;
    }
    in_frame_ = true;
    const auto colon = line.find(':');
    const std::string field = line.substr(0, colon);
    std::string value = colon == std::string::npos
                            ? ""
                            : line.substr(colon + 1);
    if (!value.empty() && value.front() == ' ') value.erase(0, 1);
    if (field == "data") {
      if (!cur_.data.empty()) cur_.data += '\n';
      cur_.data += value;
    } else if (field == "event") {
      cur_.event = value;
    } else if (field == "id") {
      cur_.id = value;
    } else if (field == "retry") {
      cur_.retry = value;
    }
    // comments/unknown fields ignored per spec
  }
  return out;
}

}  // namespace stellar::inline v1
