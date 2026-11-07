// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/iso20022/validate.hpp"

#include <algorithm>
#include <cctype>

namespace stellar::inline v1::iso20022 {

namespace {

// Active ISO 4217 alphabetic codes (same set as stellar-go-cli; XXX is the
// "no currency" code we emit for non-ISO assets).
constexpr const char* kIso4217[] = {
    "AED", "AFN", "ALL", "AMD", "ANG", "AOA", "ARS", "AUD", "AWG", "AZN",
    "BAM", "BBD", "BDT", "BGN", "BHD", "BIF", "BMD", "BND", "BOB", "BOV",
    "BRL", "BSD", "BTN", "BWP", "BYN", "BZD", "CAD", "CDF", "CHE", "CHF",
    "CHW", "CLF", "CLP", "CNY", "COP", "COU", "CRC", "CUC", "CUP", "CVE",
    "CZK", "DJF", "DKK", "DOP", "DZD", "EGP", "ERN", "ETB", "EUR", "FJD",
    "FKP", "GBP", "GEL", "GHS", "GIP", "GMD", "GNF", "GTQ", "GYD", "HKD",
    "HNL", "HRK", "HTG", "HUF", "IDR", "ILS", "INR", "IQD", "IRR", "ISK",
    "JMD", "JOD", "JPY", "KES", "KGS", "KHR", "KMF", "KPW", "KRW", "KWD",
    "KYD", "KZT", "LAK", "LBP", "LKR", "LRD", "LSL", "LYD", "MAD", "MDL",
    "MGA", "MKD", "MMK", "MNT", "MOP", "MRU", "MUR", "MVR", "MWK", "MXN",
    "MXV", "MYR", "MZN", "NAD", "NGN", "NIO", "NOK", "NPR", "NZD", "OMR",
    "PAB", "PEN", "PGK", "PHP", "PKR", "PLN", "PYG", "QAR", "RON", "RSD",
    "RUB", "RWF", "SAR", "SBD", "SCR", "SDG", "SEK", "SGD", "SHP", "SLE",
    "SLL", "SOS", "SRD", "SSP", "STN", "SVC", "SYP", "SZL", "THB", "TJS",
    "TMT", "TND", "TOP", "TRY", "TTD", "TWD", "TZS", "UAH", "UGX", "USD",
    "USN", "UYI", "UYU", "UYW", "UZS", "VED", "VES", "VND", "VUV", "WST",
    "XAF", "XAG", "XAU", "XBA", "XBB", "XBC", "XBD", "XCD", "XDR", "XOF",
    "XPD", "XPF", "XPT", "XSU", "XTS", "XUA", "XXX", "YER", "ZAR", "ZMW",
    "ZWL",
};

// ISO 3166-1 alpha-2 codes.
constexpr const char* kIso3166[] = {
    "AD", "AE", "AF", "AG", "AI", "AL", "AM", "AO", "AQ", "AR", "AS", "AT",
    "AU", "AW", "AX", "AZ", "BA", "BB", "BD", "BE", "BF", "BG", "BH", "BI",
    "BJ", "BL", "BM", "BN", "BO", "BQ", "BR", "BS", "BT", "BV", "BW", "BY",
    "BZ", "CA", "CC", "CD", "CF", "CG", "CH", "CI", "CK", "CL", "CM", "CN",
    "CO", "CR", "CU", "CV", "CW", "CX", "CY", "CZ", "DE", "DJ", "DK", "DM",
    "DO", "DZ", "EC", "EE", "EG", "EH", "ER", "ES", "ET", "FI", "FJ", "FK",
    "FM", "FO", "FR", "GA", "GB", "GD", "GE", "GF", "GG", "GH", "GI", "GL",
    "GM", "GN", "GP", "GQ", "GR", "GS", "GT", "GU", "GW", "GY", "HK", "HM",
    "HN", "HR", "HT", "HU", "ID", "IE", "IL", "IM", "IN", "IO", "IQ", "IR",
    "IS", "IT", "JE", "JM", "JO", "JP", "KE", "KG", "KH", "KI", "KM", "KN",
    "KP", "KR", "KW", "KY", "KZ", "LA", "LB", "LC", "LI", "LK", "LR", "LS",
    "LT", "LU", "LV", "LY", "MA", "MC", "MD", "ME", "MF", "MG", "MH", "MK",
    "ML", "MM", "MN", "MO", "MP", "MQ", "MR", "MS", "MT", "MU", "MV", "MW",
    "MX", "MY", "MZ", "NA", "NC", "NE", "NF", "NG", "NI", "NL", "NO", "NP",
    "NR", "NU", "NZ", "OM", "PA", "PE", "PF", "PG", "PH", "PK", "PL", "PM",
    "PN", "PR", "PS", "PT", "PW", "PY", "QA", "RE", "RO", "RS", "RU", "RW",
    "SA", "SB", "SC", "SD", "SE", "SG", "SH", "SI", "SJ", "SK", "SL", "SM",
    "SN", "SO", "SR", "SS", "ST", "SV", "SX", "SY", "SZ", "TC", "TD", "TF",
    "TG", "TH", "TJ", "TK", "TL", "TM", "TN", "TO", "TR", "TT", "TV", "TW",
    "TZ", "UA", "UG", "UM", "US", "UY", "UZ", "VA", "VC", "VE", "VG", "VI",
    "VN", "VU", "WF", "WS", "YE", "YT", "ZA", "ZM", "ZW",
};

[[nodiscard]] bool in_table(std::string_view s, const char* const* t, std::size_t n) {
  return std::binary_search(t, t + n, s,
                            [](std::string_view a, std::string_view b) { return a < b; });
}

[[nodiscard]] bool all_of(std::string_view s, bool (*pred)(char)) {
  return std::all_of(s.begin(), s.end(), [pred](char c) { return pred(c); });
}

bool is_upper_alpha(char c) { return c >= 'A' && c <= 'Z'; }
bool is_alnum_upper(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
bool is_digit_c(char c) { return c >= '0' && c <= '9'; }
bool is_hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/// mod-97 over the IBAN check digits (ISO 13616). Returns false on bad
/// shape too — callers check shape first when they want a precise message.
bool iban_mod97(std::string_view iban) {
  int rem = 0;
  // rotate: chars 4..n then 0..3
  for (std::size_t k = 0; k < iban.size(); ++k) {
    const char c = iban[(k + 4) % iban.size()];
    if (c >= '0' && c <= '9') {
      rem = (rem * 10 + (c - '0')) % 97;
    } else if (c >= 'A' && c <= 'Z') {
      const int v = c - 'A' + 10;
      rem = (rem * 10 + v / 10) % 97;
      rem = (rem * 10 + v % 10) % 97;
    } else {
      return false;
    }
  }
  return rem == 1;
}

void issue(ValidationReport& rep, std::string path, std::string msg) {
  rep.issues.push_back(ValidationIssue{std::move(path), std::move(msg)});
}

void check_max(ValidationReport& rep, std::string path, std::string_view v, std::size_t max) {
  if (v.size() > max) {
    issue(rep, std::move(path), "exceeds Max" + std::to_string(max) + "Text");
  }
}

void check_ccy_amt(ValidationReport& rep, const std::string& path, const CcyAmount& a,
                   std::size_t max_frac) {
  if (!is_iso4217(a.ccy)) {
    issue(rep, path + "/@Ccy", "not an ISO 4217 currency code: \"" + a.ccy + "\"");
  }
  const auto dot = a.value.find('.');
  if (a.value.empty() ||
      (dot == std::string::npos && !all_of(a.value, is_digit_c))) {
    issue(rep, path, "not a decimal amount: \"" + a.value + "\"");
    return;
  }
  if (dot != std::string::npos) {
    const std::string_view frac(a.value.data() + dot + 1, a.value.size() - dot - 1);
    const std::string_view whole(a.value.data(), dot);
    if (whole.empty() || !all_of(whole, is_digit_c) || frac.empty() ||
        !all_of(frac, is_digit_c)) {
      issue(rep, path, "not a decimal amount: \"" + a.value + "\"");
      return;
    }
    if (frac.size() > max_frac) {
      issue(rep, path, "exceeds " + std::to_string(max_frac) + " fraction digits");
    }
  }
}

void check_agent(ValidationReport& rep, const std::string& path, const Agent& a) {
  if (!a.bicfi.empty() && !is_bic(a.bicfi)) {
    issue(rep, path + "/BICFI", "invalid BIC: \"" + a.bicfi + "\"");
  }
  if (!a.lei.empty() && !is_lei(a.lei)) {
    issue(rep, path + "/LEI", "invalid LEI: \"" + a.lei + "\"");
  }
}

void check_account(ValidationReport& rep, const std::string& path, const CashAccount& a) {
  if (a.id) {
    if (!a.id->iban.empty() && !is_iban(a.id->iban)) {
      issue(rep, path + "/Id/IBAN", "invalid IBAN: \"" + a.id->iban + "\"");
    }
    if (a.id->othr) check_max(rep, path + "/Id/Othr/Id", a.id->othr->id, 34);
  }
  if (a.prxy) check_max(rep, path + "/Prxy/Id", a.prxy->id, 2048);
}

void check_party(ValidationReport& rep, const std::string& path, const PartyIdentification& p) {
  check_max(rep, path + "/Nm", p.nm, 140);
  if (p.id && !p.id->any_bic.empty() && !is_bic(p.id->any_bic)) {
    issue(rep, path + "/Id/OrgId/AnyBIC", "invalid BIC: \"" + p.id->any_bic + "\"");
  }
  if (p.id && !p.id->lei.empty() && !is_lei(p.id->lei)) {
    issue(rep, path + "/Id/OrgId/LEI", "invalid LEI: \"" + p.id->lei + "\"");
  }
  if (!p.ctry_of_res.empty() && !is_iso3166(p.ctry_of_res)) {
    issue(rep, path + "/CtryOfRes", "not an ISO 3166 alpha-2 code: \"" + p.ctry_of_res + "\"");
  }
  if (p.pstl_adr && !p.pstl_adr->ctry.empty() && !is_iso3166(p.pstl_adr->ctry)) {
    issue(rep, path + "/PstlAdr/Ctry", "not an ISO 3166 alpha-2 code: \"" + p.pstl_adr->ctry + "\"");
  }
}

void check_rmt_inf(ValidationReport& rep, const std::string& path, const RemittanceInfo& r) {
  for (std::size_t i = 0; i < r.ustrd.size(); ++i)
    check_max(rep, path + "/Ustrd[" + std::to_string(i) + "]", r.ustrd[i], 140);
  for (std::size_t i = 0; i < r.strd.size(); ++i)
    check_max(rep, path + "/Strd[" + std::to_string(i) + "]/RfrdDocInf/Nb",
              r.strd[i].rfrd_doc_nb, 35);
}

void check_pmt_id(ValidationReport& rep, const std::string& path, const PaymentIdentification& id) {
  check_max(rep, path + "/InstrId", id.instr_id, 35);
  if (id.end_to_end_id.empty()) {
    issue(rep, path + "/EndToEndId", "required (Max35Text)");
  } else {
    check_max(rep, path + "/EndToEndId", id.end_to_end_id, 35);
  }
  check_max(rep, path + "/TxId", id.tx_id, 35);
  if (!id.uetr.empty() && !is_uetr(id.uetr)) {
    issue(rep, path + "/UETR", "not a UUID (UETR format): \"" + id.uetr + "\"");
  }
}

void check_grp_hdr(ValidationReport& rep, const GroupHeader& h) {
  if (h.msg_id.empty()) issue(rep, "GrpHdr/MsgId", "required (Max35Text)");
  check_max(rep, "GrpHdr/MsgId", h.msg_id, 35);
  if (h.cre_dt_tm.empty()) issue(rep, "GrpHdr/CreDtTm", "required (ISODateTime)");
  if (h.instg_agt) check_agent(rep, "GrpHdr/InstgAgt/FinInstnId", *h.instg_agt);
  if (h.instd_agt) check_agent(rep, "GrpHdr/InstdAgt/FinInstnId", *h.instd_agt);
}

}  // namespace

bool is_bic(std::string_view s) noexcept {
  const auto n = s.size();
  if (n != 8 && n != 11) return false;
  if (!all_of(s.substr(0, 4), is_upper_alpha)) return false;
  if (!all_of(s.substr(4, 2), is_upper_alpha)) return false;
  if (!all_of(s.substr(6, 2), is_alnum_upper)) return false;
  return n == 8 || all_of(s.substr(8, 3), is_alnum_upper);
}

bool is_iban(std::string_view s) noexcept {
  if (s.size() < 5 || s.size() > 34) return false;
  if (!all_of(s.substr(0, 2), is_upper_alpha)) return false;
  if (!all_of(s.substr(2, 2), is_digit_c)) return false;
  if (!all_of(s.substr(2), is_alnum_upper)) return false;
  return iban_mod97(s);
}

bool is_iso4217(std::string_view s) noexcept {
  return s.size() == 3 && in_table(s, kIso4217, std::size(kIso4217));
}

bool is_iso3166(std::string_view s) noexcept {
  return s.size() == 2 && in_table(s, kIso3166, std::size(kIso3166));
}

bool is_lei(std::string_view s) noexcept {
  return s.size() == 20 && all_of(s.substr(0, 18), is_alnum_upper) &&
         all_of(s.substr(18, 2), is_digit_c);
}

bool is_uetr(std::string_view s) noexcept {
  if (s.size() != 36) return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
    if (dash != (s[i] == '-')) return false;
    if (!dash && !is_hex(s[i])) return false;
  }
  return true;
}

ValidationReport validate(const Document& doc) {
  ValidationReport rep;
  std::visit([&](const auto& m) {
    using T = std::decay_t<decltype(m)>;
    if constexpr (std::is_same_v<T, Pacs008>) {
      check_grp_hdr(rep, m.grp_hdr);
      for (std::size_t i = 0; i < m.txs.size(); ++i) {
        const auto& t = m.txs[i];
        const std::string p = "CdtTrfTxInf[" + std::to_string(i) + "]";
        check_pmt_id(rep, p + "/PmtId", t.pmt_id);
        check_ccy_amt(rep, p + "/IntrBkSttlmAmt", t.intrbk_sttlm_amt, 5);
        if (t.instd_amt) check_ccy_amt(rep, p + "/InstdAmt", *t.instd_amt, 5);
        check_party(rep, p + "/Dbtr", t.dbtr);
        check_agent(rep, p + "/DbtrAgt/FinInstnId", t.dbtr_agt);
        check_agent(rep, p + "/CdtrAgt/FinInstnId", t.cdtr_agt);
        check_party(rep, p + "/Cdtr", t.cdtr);
        if (t.dbtr_acct) check_account(rep, p + "/DbtrAcct", *t.dbtr_acct);
        if (t.cdtr_acct) check_account(rep, p + "/CdtrAcct", *t.cdtr_acct);
        if (t.rmt_inf) check_rmt_inf(rep, p + "/RmtInf", *t.rmt_inf);
      }
    }
    if constexpr (std::is_same_v<T, Pain001>) {
      check_grp_hdr(rep, m.grp_hdr);
      if (!m.grp_hdr.initg_pty) issue(rep, "GrpHdr/InitgPty", "required in pain.001");
      for (std::size_t i = 0; i < m.pmts.size(); ++i) {
        const auto& p = m.pmts[i];
        const std::string pp = "PmtInf[" + std::to_string(i) + "]";
        check_max(rep, pp + "/PmtInfId", p.pmt_inf_id, 35);
        if (p.reqd_exctn_dt.empty()) issue(rep, pp + "/ReqdExctnDt", "required");
        check_party(rep, pp + "/Dbtr", p.dbtr);
        check_account(rep, pp + "/DbtrAcct", p.dbtr_acct);
        check_agent(rep, pp + "/DbtrAgt/FinInstnId", p.dbtr_agt);
        for (std::size_t j = 0; j < p.txs.size(); ++j) {
          const auto& t = p.txs[j];
          const std::string tp = pp + "/CdtTrfTxInf[" + std::to_string(j) + "]";
          check_max(rep, tp + "/PmtId/EndToEndId", t.pmt_id.end_to_end_id, 35);
          if (t.pmt_id.end_to_end_id.empty())
            issue(rep, tp + "/PmtId/EndToEndId", "required (Max35Text)");
          if (!t.pmt_id.uetr.empty() && !is_uetr(t.pmt_id.uetr))
            issue(rep, tp + "/PmtId/UETR", "not a UUID");
          check_ccy_amt(rep, tp + "/Amt/InstdAmt", t.instd_amt, 5);
          if (t.cdtr) check_party(rep, tp + "/Cdtr", *t.cdtr);
          if (t.cdtr_acct) check_account(rep, tp + "/CdtrAcct", *t.cdtr_acct);
          if (t.cdtr_agt) check_agent(rep, tp + "/CdtrAgt/FinInstnId", *t.cdtr_agt);
          if (t.rmt_inf) check_rmt_inf(rep, tp + "/RmtInf", *t.rmt_inf);
        }
      }
    }
    if constexpr (std::is_same_v<T, Pacs002> || std::is_same_v<T, Pain002>) {
      check_grp_hdr(rep, m.grp_hdr);
      auto check_tx = [&](const PaymentTxStatus& t, const std::string& p) {
        check_max(rep, p + "/OrgnlInstrId", t.orgnl_instr_id, 35);
        check_max(rep, p + "/OrgnlEndToEndId", t.orgnl_end_to_end_id, 35);
        check_max(rep, p + "/OrgnlTxId", t.orgnl_tx_id, 35);
        if (!t.orgnl_uetr.empty() && !is_uetr(t.orgnl_uetr))
          issue(rep, p + "/OrgnlUETR", "not a UUID");
      };
      if constexpr (std::is_same_v<T, Pacs002>) {
        for (std::size_t i = 0; i < m.txs.size(); ++i)
          check_tx(m.txs[i], "TxInfAndSts[" + std::to_string(i) + "]");
      } else {
        for (std::size_t i = 0; i < m.pmts.size(); ++i)
          for (std::size_t j = 0; j < m.pmts[i].txs.size(); ++j)
            check_tx(m.pmts[i].txs[j],
                     "OrgnlPmtInfAndSts[" + std::to_string(i) + "]/TxInfAndSts[" +
                         std::to_string(j) + "]");
      }
    }
    if constexpr (std::is_same_v<T, Pacs004>) {
      check_grp_hdr(rep, m.grp_hdr);
      for (std::size_t i = 0; i < m.txs.size(); ++i) {
        const auto& t = m.txs[i];
        const std::string p = "TxInf[" + std::to_string(i) + "]";
        check_max(rep, p + "/RtrId", t.rtr_id, 35);
        check_max(rep, p + "/OrgnlEndToEndId", t.orgnl_end_to_end_id, 35);
        check_ccy_amt(rep, p + "/RtrdIntrBkSttlmAmt", t.rtrd_intrbk_sttlm_amt, 5);
      }
    }
    if constexpr (std::is_same_v<T, Camt053> || std::is_same_v<T, Camt054>) {
      check_grp_hdr(rep, m.grp_hdr);
      auto check_entries = [&](const std::vector<Entry>& es, const std::string& base) {
        for (std::size_t i = 0; i < es.size(); ++i) {
          const std::string p = base + "/Ntry[" + std::to_string(i) + "]";
          check_ccy_amt(rep, p + "/Amt", es[i].amt, 5);
          if (es[i].cdt_dbt_ind != "CRDT" && es[i].cdt_dbt_ind != "DBIT") {
            issue(rep, p + "/CdtDbtInd", "must be CRDT or DBIT");
          }
          check_max(rep, p + "/AcctSvcrRef", es[i].acct_svcr_ref, 35);
        }
      };
      if constexpr (std::is_same_v<T, Camt053>) {
        for (const auto& s : m.stmts) {
          check_account(rep, "Stmt/Acct", s.acct);
          check_entries(s.ntrys, "Stmt");
        }
      } else {
        for (const auto& s : m.ntfctns) {
          check_account(rep, "Ntfctn/Acct", s.acct);
          check_entries(s.ntrys, "Ntfctn");
        }
      }
    }
  }, doc);
  return rep;
}

}  // namespace stellar::inline v1::iso20022
