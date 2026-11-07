# ISO 20022 ↔ Stellar mapping

The `stellar::iso20022` component emits and parses ISO 20022 payment and
statement XML and maps it bidirectionally to Stellar payments. The mapping
conventions follow `stellar-go-cli`'s `pkg/iso20022` (same semantics, C++ API).

## Supported messages

| Document | Root element | Emitted namespace |
|---|---|---|
| pacs.008.001.14 | `FIToFICstmrCdtTrf` | `urn:iso:std:iso:20022:tech:xsd:pacs.008.001.14` |
| pacs.002.001.16 | `FIToFIPmtStsRpt` | `urn:iso:std:iso:20022:tech:xsd:pacs.002.001.16` |
| pacs.004.001.15 | `PmtRtr` | `urn:iso:std:iso:20022:tech:xsd:pacs.004.001.15` |
| pain.001.001.13 | `CstmrCdtTrfInitn` | `urn:iso:std:iso:20022:tech:xsd:pain.001.001.13` |
| pain.002.001.15 | `CstmrPmtStsRpt` | `urn:iso:std:iso:20022:tech:xsd:pain.002.001.15` |
| camt.053.001.14 | `BkToCstmrStmt` | `urn:iso:std:iso:20022:tech:xsd:camt.053.001.14` |
| camt.054.001.14 | `BkToCstmrDbtCdtNtfctn` | `urn:iso:std:iso:20022:tech:xsd:camt.054.001.14` |

Official XSDs are vendored under `iso20022/xsd/`; emitted documents are
expected to validate against them (`tools/check_iso20022_xsd.sh`).

Each message is implemented as a subset of its schema — the elements a
cross-border payment/disbursement flow actually uses. The parser ignores
unknown elements and attributes for forward compatibility; `DOCTYPE`/`ENTITY`
declarations are rejected (pugixml never expands external entities).

## API shape

- `Payment` — neutral Stellar record: `from`/`to` `Address`, `Asset`,
  `Amount`, `Memo`, caller `id`, `tx_hash`, `created_at`.
- `CreditTransferInstruction` — a `Payment` plus optional `Party` detail
  (name, postal address, contact, agent BIC, account in IBAN / `Othr` /
  `Prxy` form) and per-tx remittance lines / status / reason.
- `build_pacs008[_batch]`, `build_pain001`, `build_pacs004`,
  `build_pacs002`, `build_pain002`, `build_camt053`, `build_camt054`
  → `Result<std::string>` XML.
- `parse(xml)` → `Result<Document>` (variant per message);
  `detect_message_type(xml)` probes the `xmlns`.
- `instructions_from_pacs008/pain001` recover `CreditTransferInstruction`s
  (Stellar-resolvable account ids become `Address`; others stay unset).
- `status_reports(doc)` flattens pacs.002/pain.002 into `StatusReport`s;
  `reconcile_instructions(instrs, reports)` matches them back to the sent
  batch by `EndToEndId` → `InstrId` → `TxId` (last report wins).
- `validate(doc)` → `ValidationReport` with per-issue field paths.

## Field mapping

| Stellar | ISO 20022 | Notes |
|---|---|---|
| `Payment.amount` | `IntrBkSttlmAmt` / `InstdAmt` / `Amt` | truncated to the 5 fraction digits the schema allows |
| exact 7-dp amount | `SplmtryData/Envlp/Asset/Amt` | preserves full precision; restored on parse |
| `Payment.asset` | `Ccy` attribute | ISO 4217 code used as-is; anything else → `XXX` |
| asset code+issuer | `SplmtryData/Envlp/Asset/{Cd,Issr}` | restored on parse (`XLM`/no issuer → native) |
| `Payment.to` / `from` | `DbtrAcct`/`CdtrAcct` `Prxy/Id` | `Othr/Id` is Max34Text; strkeys are 56 chars |
| non-Stellar accounts | `Id/IBAN`, `Id/Othr` (≤34), `Prxy` | precedence IBAN → Othr → Prxy |
| `Payment.tx_hash` | `EndToEndId` | truncated to Max35Text; else `Payment.id` |
| `Payment.id` | `InstrId`/`TxId`/`NtryRef`/`StsId` | ≤35 chars |
| — | `UETR` | generated UUIDv4 unless overridden |
| `Payment.memo` text | `RmtInf/Ustrd` | memo id/hash/return → `Strd/RfrdDocInf/Nb` |
| `Payment.created_at` | `IntrBkSttlmDt`, `BookgDt`, `ValDt` | ISO date, UTC |
| party agent BIC | `*Agt/FinInstnId/BICFI` | fallback `Othr/Id` = truncated address (or `NOTPROVIDED`) |
| tx result | `TxSts` (pacs.002/pain.002) | ACSC/ACCP/ACSP/RJCT/PDNG; reason → `StsRsnInf` |
| returns (pacs.004) | `RtrdIntrBkSttlmAmt`, `RtrRsnInf`, `OrgnlTxRef` | echoes original parties |
| `GrpHdr` totals | `NbOfTxs`, `CtrlSum`, `TtlIntrBkSttlmAmt` | CtrlSum = exact decimal sum; Ttl… only when single-ccy |

## Defaults

- `ChrgBr` = `SHAR`, `SttlmInf/SttlmMtd` = `CLRG`, `PmtMtd` = `TRF`,
  `BkTxCd/Prtry/Cd` = `PMNT`, `Ntry/Sts` = `BOOK`.
- `MsgId` = `msg_id_prefix` + truncated `Payment.id` ("SCPP" prefix) unless
  `msg_id` is set.
- `InitgPty` (pain.001 mandatory, pacs.008 tx-level) = `initg_pty_name`
  ("stellar-cpp-sdk").

## Caveats

- **XLM and arbitrary Stellar assets are not ISO 4217 currencies.** The
  settlement `Ccy` is `XXX` and the real asset travels in `SplmtryData` —
  strict ISO consumers see "no currency"; Stellar-aware ones get full
  fidelity back. Note a Stellar asset literally *coded* `USD`/`EUR` is
  emitted under that ISO code (matching the reference implementation).
- Parser binds by local element names with no namespace prefix support —
  `<Document xmlns=…>` documents only (the universal convention for these
  messages).
- `pain.002` targets `.001.15` (latest published); its `TxInfAndSts` omits
  `OrgnlTxId` per the schema — parse accepts the exact emitted namespace
  only.
- No `head.001` AppHdr envelope (SWIFT transport wrapper) — deferred.
