// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file validate.hpp
/// Field-level ISO 20022 validation: identifier formats (BIC, IBAN, LEI,
/// ISO 4217, ISO 3166) and a document-wide ValidationReport that collects
/// every issue with its field path — compliance checks want the full list,
/// not the first failure.

#include "stellar/export.hpp"
#include "stellar/iso20022/documents.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace stellar::inline v1::iso20022 {

[[nodiscard]] STELLAR_API bool is_bic(std::string_view s) noexcept;   // ISO 9362 BICFI
[[nodiscard]] STELLAR_API bool is_iban(std::string_view s) noexcept;  // ISO 13616 + mod-97
[[nodiscard]] STELLAR_API bool is_iso4217(std::string_view s) noexcept;
[[nodiscard]] STELLAR_API bool is_iso3166(std::string_view s) noexcept;  // alpha-2 country
[[nodiscard]] STELLAR_API bool is_lei(std::string_view s) noexcept;
[[nodiscard]] STELLAR_API bool is_uetr(std::string_view s) noexcept;  // UUIDv4 format

struct ValidationIssue {
  std::string path;  // e.g. "CdtTrfTxInf[0]/DbtrAgt/FinInstnId/BICFI"
  std::string message;
  bool operator==(const ValidationIssue&) const = default;
};

struct ValidationReport {
  std::vector<ValidationIssue> issues;
  [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
};

/// Validate a document against the implemented subset's format rules
/// (identifiers, amounts, text lengths, required fields of the subset).
[[nodiscard]] STELLAR_API ValidationReport validate(const Document& doc);

}  // namespace stellar::inline v1::iso20022
