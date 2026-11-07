// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/iso20022/xml.hpp"

#include "detail/messages.hpp"

#include <sstream>

namespace stellar::inline v1::iso20022 {

MessageKind message_kind(const Document& d) noexcept {
  return std::visit(
      [](const auto& m) -> MessageKind {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, Pacs008>) return MessageKind::kPacs008;
        if constexpr (std::is_same_v<T, Pacs002>) return MessageKind::kPacs002;
        if constexpr (std::is_same_v<T, Pacs004>) return MessageKind::kPacs004;
        if constexpr (std::is_same_v<T, Pain001>) return MessageKind::kPain001;
        if constexpr (std::is_same_v<T, Pain002>) return MessageKind::kPain002;
        if constexpr (std::is_same_v<T, Camt053>) return MessageKind::kCamt053;
        return MessageKind::kCamt054;
      },
      d);
}

std::string_view message_id(MessageKind k) noexcept {
  switch (k) {
    case MessageKind::kPacs008:
      return "pacs.008.001.14";
    case MessageKind::kPacs002:
      return "pacs.002.001.16";
    case MessageKind::kPacs004:
      return "pacs.004.001.15";
    case MessageKind::kPain001:
      return "pain.001.001.13";
    case MessageKind::kPain002:
      return "pain.002.001.15";
    case MessageKind::kCamt053:
      return "camt.053.001.14";
    case MessageKind::kCamt054:
      return "camt.054.001.14";
  }
  return {};
}

std::string_view message_namespace(MessageKind k) noexcept {
  switch (k) {
    case MessageKind::kPacs008:
      return "urn:iso:std:iso:20022:tech:xsd:pacs.008.001.14";
    case MessageKind::kPacs002:
      return "urn:iso:std:iso:20022:tech:xsd:pacs.002.001.16";
    case MessageKind::kPacs004:
      return "urn:iso:std:iso:20022:tech:xsd:pacs.004.001.15";
    case MessageKind::kPain001:
      return "urn:iso:std:iso:20022:tech:xsd:pain.001.001.13";
    case MessageKind::kPain002:
      return "urn:iso:std:iso:20022:tech:xsd:pain.002.001.15";
    case MessageKind::kCamt053:
      return "urn:iso:std:iso:20022:tech:xsd:camt.053.001.14";
    case MessageKind::kCamt054:
      return "urn:iso:std:iso:20022:tech:xsd:camt.054.001.14";
  }
  return {};
}

Result<std::string> to_xml(const Document& doc) {
  const MessageKind kind = message_kind(doc);

  // Schema-required members of the implemented subset — catch gaps at emit
  // time rather than producing XML that cannot validate.
  if (const auto* p8 = std::get_if<Pacs008>(&doc)) {
    if (!p8->grp_hdr.sttlm_inf) {
      return Error::invalid_argument("pacs.008: GrpHdr/SttlmInf is required");
    }
    if (p8->grp_hdr.nb_of_txs.empty()) {
      return Error::invalid_argument("pacs.008: GrpHdr/NbOfTxs is required");
    }
  }
  if (const auto* p1 = std::get_if<Pain001>(&doc)) {
    if (!p1->grp_hdr.initg_pty) {
      return Error::invalid_argument("pain.001: GrpHdr/InitgPty is required");
    }
    if (p1->pmts.empty()) {
      return Error::invalid_argument("pain.001: at least one PmtInf is required");
    }
  }

  pugi::xml_document d;
  auto decl = d.append_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";
  auto root = d.append_child("Document");
  const std::string ns(message_namespace(kind));
  root.append_attribute("xmlns") = ns.c_str();

  std::visit(
      [root](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, Pacs008>) detail::emit_pacs008(root, m);
        if constexpr (std::is_same_v<T, Pacs002>) detail::emit_pacs002(root, m);
        if constexpr (std::is_same_v<T, Pacs004>) detail::emit_pacs004(root, m);
        if constexpr (std::is_same_v<T, Pain001>) detail::emit_pain001(root, m);
        if constexpr (std::is_same_v<T, Pain002>) detail::emit_pain002(root, m);
        if constexpr (std::is_same_v<T, Camt053>) detail::emit_camt053(root, m);
        if constexpr (std::is_same_v<T, Camt054>) detail::emit_camt054(root, m);
      },
      doc);

  std::ostringstream oss;
  d.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);
  return oss.str();
}

}  // namespace stellar::inline v1::iso20022
