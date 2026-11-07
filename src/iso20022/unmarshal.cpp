// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/iso20022/xml.hpp"

#include "detail/messages.hpp"

#include <cctype>

namespace stellar::inline v1::iso20022 {

namespace {

/// Cheap prolog scan: DOCTYPE/ENTITY declarations are rejected before the DOM
/// load. pugixml never expands external entities, but a hard reject keeps the
/// contract obvious and matches what XSD-bound consumers expect.
Result<void> reject_dtd(std::string_view xml) {
  const auto head = xml.substr(0, xml.size() < 4096 ? xml.size() : 4096);
  std::string lower(head.size(), '\0');
  for (std::size_t i = 0; i < head.size(); ++i)
    lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(head[i])));
  if (lower.find("<!doctype") != std::string::npos || lower.find("<!entity") != std::string::npos) {
    return Error{ErrorCode::kProtocolError,
                 std::string("iso20022: DOCTYPE/ENTITY declarations are not supported")};
  }
  return {};
}

Result<pugi::xml_node> load_document(std::string_view xml, pugi::xml_document& out) {
  STELLAR_TRY(reject_dtd(xml));
  const auto res =
      out.load_buffer(xml.data(), xml.size(), pugi::parse_default | pugi::parse_trim_pcdata);
  if (!res) {
    return Error{ErrorCode::kProtocolError, std::string("iso20022: XML parse failed at offset ") +
                                                std::to_string(res.offset) + ": " +
                                                res.description()};
  }
  auto doc = out.child("Document");
  if (!doc) {
    return Error{
        ErrorCode::kProtocolError,
        std::string("iso20022: not an ISO 20022 document (root element is not <Document>)")};
  }
  return doc;
}

/// xmlns → MessageKind for the versions this build supports.
std::optional<MessageKind> kind_for_namespace(std::string_view ns) {
  for (int i = 0; i <= static_cast<int>(MessageKind::kCamt054); ++i) {
    const auto k = static_cast<MessageKind>(i);
    if (message_namespace(k) == ns) return k;
  }
  return std::nullopt;
}

}  // namespace

Result<MessageKind> detect_message_type(std::string_view xml) {
  pugi::xml_document d;
  STELLAR_TRY_ASSIGN(auto doc, load_document(xml, d));
  const std::string_view ns = doc.attribute("xmlns").value();
  if (auto k = kind_for_namespace(ns)) return *k;
  return Error{ErrorCode::kProtocolError,
               std::string("iso20022: unrecognized or unsupported ISO 20022 namespace \"") +
                   std::string(ns) + "\""};
}

Result<Document> parse(std::string_view xml) {
  pugi::xml_document d;
  STELLAR_TRY_ASSIGN(auto doc, load_document(xml, d));
  const std::string_view ns = doc.attribute("xmlns").value();
  const auto kind = kind_for_namespace(ns);
  if (!kind) {
    return Error{ErrorCode::kProtocolError,
                 std::string("iso20022: unrecognized or unsupported ISO 20022 namespace \"") +
                     std::string(ns) + "\""};
  }
  switch (*kind) {
    case MessageKind::kPacs008: {
      STELLAR_TRY_ASSIGN(auto m, detail::read_pacs008(doc));
      return Document{std::move(m)};
    }
    case MessageKind::kPacs002: {
      STELLAR_TRY_ASSIGN(auto m, detail::read_pacs002(doc));
      return Document{std::move(m)};
    }
    case MessageKind::kPacs004: {
      STELLAR_TRY_ASSIGN(auto m, detail::read_pacs004(doc));
      return Document{std::move(m)};
    }
    case MessageKind::kPain001: {
      STELLAR_TRY_ASSIGN(auto m, detail::read_pain001(doc));
      return Document{std::move(m)};
    }
    case MessageKind::kPain002: {
      STELLAR_TRY_ASSIGN(auto m, detail::read_pain002(doc));
      return Document{std::move(m)};
    }
    case MessageKind::kCamt053: {
      STELLAR_TRY_ASSIGN(auto m, detail::read_camt053(doc));
      return Document{std::move(m)};
    }
    case MessageKind::kCamt054: {
      STELLAR_TRY_ASSIGN(auto m, detail::read_camt054(doc));
      return Document{std::move(m)};
    }
  }
  return Error{ErrorCode::kInternal, std::string("iso20022: unhandled message kind")};
}

}  // namespace stellar::inline v1::iso20022
