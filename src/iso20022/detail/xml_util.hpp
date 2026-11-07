// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file xml_util.hpp (private)
/// Thin pugixml helpers shared by the per-message emitters and readers.
/// Emitters append only what is set; readers report required-element gaps in
/// an error list keyed by field path.

#include <pugixml.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1::iso20022::detail {

// ---------------------------------------------------------------------------
// Emit
// ---------------------------------------------------------------------------

/// `<name>text</name>`.
inline void add_el(pugi::xml_node parent, const char* name, std::string_view text) {
  const std::string s(text);
  parent.append_child(name).text().set(s.c_str());
}

/// Like add_el but skipped when `text` is empty.
inline void add_el_if(pugi::xml_node parent, const char* name, const std::string& text) {
  if (!text.empty()) add_el(parent, name, text);
}

template <typename T>
inline void add_el_if(pugi::xml_node parent, const char* name, const std::optional<T>& text) {
  if (text && !text->empty()) add_el(parent, name, *text);
}

/// `<name Ccy="ccy">value</name>` — ActiveOrHistoricCurrencyAndAmount & friends.
inline void add_amt(pugi::xml_node parent, const char* name, std::string_view ccy,
                    std::string_view value) {
  auto n = parent.append_child(name);
  const std::string c(ccy);
  const std::string v(value);
  n.append_attribute("Ccy") = c.c_str();
  n.text().set(v.c_str());
}

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------

/// Trimmed text of `node` — nullopt when the node or its text is absent.
[[nodiscard]] inline std::optional<std::string> opt_text(pugi::xml_node node) {
  if (!node) return std::nullopt;
  const char* t = node.text().get();
  if (!t || !*t) return std::nullopt;
  const std::string s(t);
  const auto b = s.find_first_not_of(" \t\r\n");
  const auto e = s.find_last_not_of(" \t\r\n");
  if (b == std::string::npos) return std::nullopt;
  return s.substr(b, e - b + 1);
}

/// opt_text of child `name` under `node` — "" when absent.
[[nodiscard]] inline std::string child_text(pugi::xml_node node, const char* name) {
  return opt_text(node.child(name)).value_or("");
}

/// Required child element text; records `path/name` in `errs` when missing.
inline void req_text(pugi::xml_node node, const char* name, std::string& out,
                     const std::string& path, std::vector<std::string>& errs) {
  if (auto v = opt_text(node.child(name))) {
    out = std::move(*v);
  } else {
    errs.push_back(path + "/" + name + ": required element missing");
  }
}

}  // namespace stellar::inline v1::iso20022::detail
