// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/http.hpp"

#include <curl/curl.h>

#include <mutex>

namespace stellar::inline v1 {

namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
  static_cast<std::string*>(ud)->append(ptr, size * nmemb);
  return size * nmemb;
}
size_t header_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
  const size_t n = size * nmemb;
  std::string_view line(ptr, n);
  const auto colon = line.find(':');
  if (colon != std::string_view::npos) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(ud);
    std::string k{line.substr(0, colon)};
    std::string v{line.substr(colon + 1)};
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
    (*headers)[std::move(k)] = std::move(v);
  }
  return n;
}

}  // namespace

CurlTransport::CurlTransport() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}
CurlTransport::~CurlTransport() = default;  // global cleanup left to process exit

Result<HttpResponse> CurlTransport::perform(const HttpRequest& request) {
  CURL* c = curl_easy_init();
  if (!c) return Error::internal("curl_easy_init failed");

  curl_easy_setopt(c, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_ms));
  curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent_.c_str());
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 0L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, tls_verify_ ? 1L : 0L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, tls_verify_ ? 2L : 0L);
  curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);

  struct curl_slist* hdrs = nullptr;
  for (const auto& [k, v] : request.headers) {
    hdrs = curl_slist_append(hdrs, (k + ": " + v).c_str());
  }
  if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

  if (request.method == "POST") {
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
  } else if (request.method != "GET") {
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (!request.body.empty()) {
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, request.body.data());
      curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE,
                       static_cast<curl_off_t>(request.body.size()));
    }
  }

  HttpResponse res;
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &res.body);
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt(c, CURLOPT_HEADERDATA, &res.headers);

  const CURLcode rc = curl_easy_perform(c);
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res.status);
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(c);

  if (rc != CURLE_OK) {
    return Error{ErrorCode::kNetwork,
                 std::string("HTTP transport failed: ") + curl_easy_strerror(rc)};
  }
  return res;
}

Result<long> CurlTransport::stream(const HttpRequest& request, StreamSink& sink,
                                   CancelToken* cancel) {
  CURL* c = curl_easy_init();
  if (!c) return Error::internal("curl_easy_init failed");
  curl_easy_setopt(c, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_ms));
  curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent_.c_str());
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, tls_verify_ ? 1L : 0L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, tls_verify_ ? 2L : 0L);
  struct curl_slist* hdrs = nullptr;
  for (const auto& [k, v] : request.headers) hdrs = curl_slist_append(hdrs, (k + ": " + v).c_str());
  if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
  if (request.method == "POST") {
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
  }
  struct Ctx {
    StreamSink* sink;
    CancelToken* cancel;
    bool abort{false};
    std::map<std::string, std::string> headers;
  };
  Ctx ctx{&sink, cancel, false, {}};
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(
      c, CURLOPT_WRITEFUNCTION,
      +[](char* p, std::size_t sz, std::size_t nm, void* ud) -> std::size_t {
        auto* x = static_cast<Ctx*>(ud);
        const std::size_t n = sz * nm;
        if ((x->cancel && x->cancel->cancelled()) || !x->sink->on_data(p, n)) {
          x->abort = true;
          return 0;
        }
        return n;
      });
  std::map<std::string, std::string> h;
  curl_easy_setopt(c, CURLOPT_HEADERDATA, &h);
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
  const CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(c);
  if (ctx.abort) return Error{ErrorCode::kInvalidArgument, "stream aborted"};
  if (rc != CURLE_OK) {
    return Error{ErrorCode::kNetwork, std::string("HTTP stream failed: ") + curl_easy_strerror(rc)};
  }
  sink.on_headers(status, h);
  return status;
}

}  // namespace stellar::inline v1
