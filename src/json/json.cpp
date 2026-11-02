// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/json.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace stellar::inline v1 {

const Json* Json::find(std::string_view key) const noexcept {
  if (!is_object()) return nullptr;
  const auto& o = as_object();
  const auto it = o.find(std::string(key));
  return it == o.end() ? nullptr : &it->second;
}

const Json& Json::at(std::string_view key) const noexcept {
  static const Json null;
  const auto* p = find(key);
  return p ? *p : null;
}

const Json& Json::at(std::size_t i) const noexcept {
  static const Json null;
  if (!is_array() || i >= as_array().size()) return null;
  return as_array()[i];
}

// ---------------------------------------------------------------------------
// Serializer
// ---------------------------------------------------------------------------

namespace {

void dump_string(std::string& out, std::string_view s) {
  out += '"';
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

}  // namespace

std::string Json::dump() const {
  std::string out;
  switch (kind()) {
    case Kind::kNull:
      out = "null";
      break;
    case Kind::kBool:
      out = as_bool() ? "true" : "false";
      break;
    case Kind::kInt:
      out = std::to_string(as_int());
      break;
    case Kind::kDouble: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.17g", as_double());
      out = buf;
      break;
    }
    case Kind::kString:
      dump_string(out, as_string());
      break;
    case Kind::kArray: {
      out = '[';
      bool first = true;
      for (const auto& e : as_array()) {
        if (!first) out += ',';
        first = false;
        out += e.dump();
      }
      out += ']';
      break;
    }
    case Kind::kObject: {
      out = '{';
      bool first = true;
      for (const auto& [k, v] : as_object()) {
        if (!first) out += ',';
        first = false;
        dump_string(out, k);
        out += ':';
        out += v.dump();
      }
      out += '}';
      break;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

struct Parser {
  std::string_view s;
  std::size_t pos = 0;
  Error err;

  Error fail(const char* m) {
    err = Error{ErrorCode::kProtocolError, std::string(m) + " at offset " + std::to_string(pos)};
    return err;
  }

  void skip_ws() {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
      ++pos;
  }

  Result<Json> value() {
    skip_ws();
    if (pos >= s.size()) return fail("unexpected end");
    switch (s[pos]) {
      case 'n':
        return lit("null", Json(nullptr));
      case 't':
        return lit("true", Json(true));
      case 'f':
        return lit("false", Json(false));
      case '"': {
        STELLAR_TRY_ASSIGN(auto str, string());
        return Json(std::move(str));
      }
      case '[':
        return array();
      case '{':
        return object();
      default:
        return number();
    }
  }

  Result<Json> lit(std::string_view word, Json v) {
    if (s.substr(pos, word.size()) != word) return fail("invalid literal");
    pos += word.size();
    return v;
  }

  Result<std::string> string() {
    std::string out;
    ++pos;  // consume "
    while (pos < s.size()) {
      const auto c = static_cast<unsigned char>(s[pos++]);
      if (c == '"') return out;
      if (c == '\\') {
        if (pos >= s.size()) return fail("truncated escape");
        const char e = s[pos++];
        switch (e) {
          case '"':
            out += '"';
            break;
          case '\\':
            out += '\\';
            break;
          case '/':
            out += '/';
            break;
          case 'b':
            out += '\b';
            break;
          case 'f':
            out += '\f';
            break;
          case 'n':
            out += '\n';
            break;
          case 'r':
            out += '\r';
            break;
          case 't':
            out += '\t';
            break;
          case 'u': {
            if (pos + 4 > s.size()) return fail("truncated \\u");
            unsigned cp = 0;
            for (int i = 0; i < 4; ++i) {
              const char h = s[pos++];
              cp <<= 4;
              if (h >= '0' && h <= '9')
                cp |= static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f')
                cp |= static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F')
                cp |= static_cast<unsigned>(h - 'A' + 10);
              else
                return fail("bad \\u hex");
            }
            if (cp < 0x80)
              out += static_cast<char>(cp);
            else if (cp < 0x800) {
              out += static_cast<char>(0xC0 | (cp >> 6));
              out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
              out += static_cast<char>(0xE0 | (cp >> 12));
              out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
              out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            break;
          }
          default:
            return fail("bad escape");
        }
      } else if (c < 0x20) {
        return fail("unescaped control char in string");
      } else {
        out += static_cast<char>(c);
      }
    }
    return fail("unterminated string");
  }

  Result<Json> number() {
    const std::size_t start = pos;
    if (pos < s.size() && s[pos] == '-') ++pos;
    if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
      return fail("invalid number");
    if (s[pos] == '0') {
      ++pos;
    } else {
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    bool frac = false;
    if (pos < s.size() && s[pos] == '.') {
      frac = true;
      ++pos;
      if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
        return fail("invalid number");
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
      frac = true;
      ++pos;
      if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
      if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
        return fail("invalid number");
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    const auto tok = s.substr(start, pos - start);
    if (!frac) {
      errno = 0;
      const long long v = std::strtoll(std::string(tok).c_str(), nullptr, 10);
      if (errno == 0) return Json(static_cast<std::int64_t>(v));
    }
    return Json(std::strtod(std::string(tok).c_str(), nullptr));
  }

  Result<Json> array() {
    ++pos;
    Json::Array out;
    skip_ws();
    if (pos < s.size() && s[pos] == ']') {
      ++pos;
      return Json(std::move(out));
    }
    while (true) {
      STELLAR_TRY_ASSIGN(auto e, value());
      out.push_back(std::move(e));
      skip_ws();
      if (pos >= s.size()) return fail("unterminated array");
      if (s[pos] == ']') {
        ++pos;
        return Json(std::move(out));
      }
      if (s[pos] != ',') return fail("expected ',' or ']'");
      ++pos;
    }
  }

  Result<Json> object() {
    ++pos;
    Json::Object out;
    skip_ws();
    if (pos < s.size() && s[pos] == '}') {
      ++pos;
      return Json(std::move(out));
    }
    while (true) {
      skip_ws();
      if (pos >= s.size() || s[pos] != '"') return fail("expected object key");
      STELLAR_TRY_ASSIGN(auto key, string());
      skip_ws();
      if (pos >= s.size() || s[pos] != ':') return fail("expected ':'");
      ++pos;
      STELLAR_TRY_ASSIGN(auto v, value());
      if (!out.emplace(std::move(key), std::move(v)).second) return fail("duplicate object key");
      skip_ws();
      if (pos >= s.size()) return fail("unterminated object");
      if (s[pos] == '}') {
        ++pos;
        return Json(std::move(out));
      }
      if (s[pos] != ',') return fail("expected ',' or '}'");
      ++pos;
    }
  }
};

}  // namespace

Result<Json> Json::parse(std::string_view text) {
  Parser p{text};
  auto v = p.value();
  if (!v.ok()) return v.error();
  p.skip_ws();
  if (p.pos != text.size()) return p.fail("trailing bytes after JSON");
  return v;
}

// ---------------------------------------------------------------------------
// JSON-RPC helpers
// ---------------------------------------------------------------------------

Json JsonRpc::request(std::string_view method, Json params, std::int64_t id) {
  Json::Object o;
  o.emplace("jsonrpc", "2.0");
  o.emplace("method", std::string(method));
  o.emplace("params", std::move(params));
  o.emplace("id", id);
  return Json(std::move(o));
}

const Json* JsonRpc::error_of(const Json& response) {
  const auto* e = response.find("error");
  return (e && e->is_object()) ? e : nullptr;
}

const Json* JsonRpc::result_of(const Json& response) {
  return response.find("result");
}

}  // namespace stellar::inline v1
