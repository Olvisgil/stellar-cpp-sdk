// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file xml.hpp
/// ISO 20022 XML (de)serialization: emit a Document to XML, detect a message
/// kind from its xmlns, parse XML back into a Document.
///
/// Parse rules: DOCTYPE is rejected outright; unknown elements and attributes
/// are ignored for forward compatibility; missing elements required by the
/// implemented subset produce kProtocolError with the field path.

#include "stellar/export.hpp"
#include "stellar/iso20022/documents.hpp"
#include "stellar/result.hpp"

#include <string>
#include <string_view>

namespace stellar::inline v1::iso20022 {

/// Probe the Document root's xmlns and return its MessageKind.
[[nodiscard]] STELLAR_API Result<MessageKind> detect_message_type(std::string_view xml);

/// Parse a `<Document xmlns="urn:iso:...">` payload into the matching
/// document struct. Namespace must exactly match a supported version —
/// a different version of the same message is a clear error, not a guess.
[[nodiscard]] STELLAR_API Result<Document> parse(std::string_view xml);

/// Serialize a document: `<?xml version="1.0" encoding="UTF-8"?>` +
/// `<Document xmlns="...">`. Element order follows the XSD sequences.
[[nodiscard]] STELLAR_API Result<std::string> to_xml(const Document& doc);

}  // namespace stellar::inline v1::iso20022
