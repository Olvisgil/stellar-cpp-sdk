// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#include "stellar/contract/contract_meta.hpp"

#include <cstring>
#include "stellar/json.hpp"
#include "stellar/xdr/codec.hpp"

namespace stellar::inline v1 {

namespace {

Result<xdr::SCVal> json_to_scval(const Json& j) {
  const auto t = j.kind();
  if (t == Json::Kind::kBool) return scval::bool_(j.as_bool());
  if (t == Json::Kind::kInt) return scval::i64(j.as_int());
  if (t == Json::Kind::kDouble) {
    return Error::invalid_argument("floating-point args unsupported");
  }
  if (t == Json::Kind::kString) return scval::string(j.as_string());
  if (t == Json::Kind::kArray) {
    std::vector<xdr::SCVal> items;
    for (const auto& e : j.as_array()) {
      STELLAR_TRY_ASSIGN(auto v, json_to_scval(e));
      items.push_back(v);
    }
    return scval::vec(std::move(items));
  }
  if (t == Json::Kind::kObject) {
    std::vector<xdr::SCMapEntry> entries;
    for (const auto& [k, v] : j.as_object()) {
      STELLAR_TRY_ASSIGN(auto kv, json_to_scval(v));
      entries.push_back(
          xdr::SCMapEntry{xdr::Box<xdr::SCVal>(scval::symbol(k)), xdr::Box<xdr::SCVal>(kv)});
    }
    return scval::map(std::move(entries));
  }
  return Error::invalid_argument("null arg unsupported");
}

// WASM LEB128 decoder (unsigned)
Result<std::uint64_t> leb(ByteSpan b, std::size_t* i) {
  std::uint64_t v = 0;
  std::uint32_t shift = 0;
  while (*i < b.size() && shift < 64) {
    const auto c = b[(*i)++];
    v |= static_cast<std::uint64_t>(c & 0x7f) << shift;
    if (!(c & 0x80)) return v;
    shift += 7;
  }
  return Error::invalid_argument("malformed LEB128");
}

// WASM name: u32 len + UTF-8 bytes
Result<std::string> name(ByteSpan b, std::size_t* i) {
  STELLAR_TRY_ASSIGN(auto n, leb(b, i));
  if (*i + n > b.size()) return Error::invalid_argument("name overruns");
  std::string s(reinterpret_cast<const char*>(b.data() + *i), n);
  *i += n;
  return s;
}

}  // namespace

Result<ContractMeta> parse_contract_meta(ByteSpan wasm) {
  if (wasm.size() < 8 || std::memcmp(wasm.data(), "\0asm", 4) != 0) {
    return Error::invalid_argument("not a WASM binary");
  }
  ContractMeta out;
  std::size_t i = 8;  // skip magic + version
  while (i < wasm.size()) {
    const std::uint8_t id = wasm[i++];
    STELLAR_TRY_ASSIGN(auto len, leb(wasm, &i));
    if (i + len > wasm.size()) return Error::invalid_argument("section overruns");
    const auto section = wasm.subspan(i, static_cast<std::size_t>(len));
    i += static_cast<std::size_t>(len);
    if (id != 0) continue;  // only custom sections carry metadata
    std::size_t p = 0;
    STELLAR_TRY_ASSIGN(auto sname, name(section, &p));
    const auto payload = section.subspan(p);
    if (sname == "contractenvmetav0") {
      // vec of strings
      std::size_t q = 0;
      STELLAR_TRY_ASSIGN(auto n, leb(payload, &q));
      for (std::uint64_t k = 0; k < n && q < payload.size(); ++k) {
        STELLAR_TRY_ASSIGN(auto len2, leb(payload, &q));
        if (q + len2 <= payload.size()) {
          out.env_meta.emplace_back(reinterpret_cast<const char*>(payload.data() + q), len2);
          q += len2;
        }
      }
    } else if (sname == "contractspecv0") {
      out.spec_entries_xdr.assign(payload.begin(), payload.end());
    } else if (sname == "contractmetav0") {
      // XDR map of string->string; parse heuristically
      std::size_t q = 0;
      STELLAR_TRY_ASSIGN(auto n, leb(payload, &q));
      for (std::uint64_t k = 0; k < n && q < payload.size(); ++k) {
        STELLAR_TRY_ASSIGN(auto kl, leb(payload, &q));
        if (q + kl > payload.size()) break;
        const std::string key(reinterpret_cast<const char*>(payload.data() + q), kl);
        q += kl;
        STELLAR_TRY_ASSIGN(auto vl, leb(payload, &q));
        if (q + vl > payload.size()) break;
        out.kvs.emplace(key, std::string(reinterpret_cast<const char*>(payload.data() + q), vl));
        q += vl;
      }
    }
  }
  if (auto it = out.kvs.find("name"); it != out.kvs.end()) out.name = it->second;
  if (auto it = out.kvs.find("version"); it != out.kvs.end()) out.version = it->second;
  return out;
}

Result<xdr::SCVal> scval_from_string(std::string_view spec_type, std::string_view arg) {
  std::string t{spec_type};
  for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t.rfind("scv_", 0) == 0) t = t.substr(4);
  if (t.rfind("sc_spec_type_", 0) == 0) t = t.substr(13);
  if (t == "bool") return scval::bool_(arg == "true" || arg == "1");
  if (t == "u32") return scval::u32(static_cast<std::uint32_t>(std::stoul(std::string{arg})));
  if (t == "i32") return scval::i32(static_cast<std::int32_t>(std::stol(std::string{arg})));
  if (t == "u64") return scval::u64(std::stoull(std::string{arg}));
  if (t == "i64") return scval::i64(std::stoll(std::string{arg}));
  if (t == "u128" || t == "i128" || t == "u256" || t == "i256") {
    const std::size_t bytes_n = (t.find("256") != std::string::npos) ? 32 : 16;
    if (arg.substr(0, 2) == "0x" || arg.substr(0, 2) == "0X") {
      auto hb = hex_decode(arg.substr(2));
      if (!hb.ok()) return hb.error();
      Bytes buf(bytes_n, 0);
      std::copy(hb->begin(), hb->begin() + std::min(hb->size(), buf.size()),
                buf.end() - static_cast<std::ptrdiff_t>(hb->size()));
      if (bytes_n == 16) {
        std::uint64_t hi{}, lo{};
        std::memcpy(&hi, buf.data(), 8);
        std::memcpy(&lo, buf.data() + 8, 8);
        return t[0] == 'u' ? scval::u128(hi, lo) : scval::i128(static_cast<std::int64_t>(hi), lo);
      }
      xdr::UInt256Parts p{};
      std::memcpy(&p, buf.data(), 32);
      return xdr::SCVal::scvU256(p);
    }
    if (bytes_n == 16) {
      if (t[0] == 'u') {
        auto b = BigInt<16, false>::from_decimal(arg);
        if (!b.ok()) return b.error();
        std::uint64_t hi{}, lo{};
        std::memcpy(&hi, b->bytes.data(), 8);
        std::memcpy(&lo, b->bytes.data() + 8, 8);
        return scval::u128(hi, lo);
      }
      auto b = BigInt<16, true>::from_decimal(arg);
      if (!b.ok()) return b.error();
      std::uint64_t hi{}, lo{};
      std::memcpy(&hi, b->bytes.data(), 8);
      std::memcpy(&lo, b->bytes.data() + 8, 8);
      return scval::i128(static_cast<std::int64_t>(hi), lo);
    }
    if (t[0] == 'u') {
      auto b = BigInt<32, false>::from_decimal(arg);
      if (!b.ok()) return b.error();
      xdr::UInt256Parts p{};
      std::memcpy(&p, b->bytes.data(), 32);
      return xdr::SCVal::scvU256(p);
    }
    auto b = BigInt<32, true>::from_decimal(arg);
    if (!b.ok()) return b.error();
    xdr::Int256Parts p{};
    std::memcpy(&p, b->bytes.data(), 32);
    return xdr::SCVal::scvI256(p);
  }
  if (t == "string") return scval::string(arg);
  if (t == "symbol") return scval::symbol(arg);
  if (t == "address") {
    auto a = Address::parse(std::string{arg});
    if (!a.ok()) return a.error();
    return scval::address(*a);
  }
  if (t == "bytes" || t.rfind("bytesn", 0) == 0) {
    auto hb = hex_decode(arg);
    if (hb.ok()) return scval::bytes(ByteSpan(hb->data(), hb->size()));
    return scval::bytes(ByteSpan(reinterpret_cast<const std::uint8_t*>(arg.data()), arg.size()));
  }
  // complex types (vec/map/tuple/option/result): accept JSON input
  auto j = Json::parse(arg);
  if (j.ok()) {
    auto v = json_to_scval(*j);
    if (v.ok()) return v;
  }
  return Error::invalid_argument("unsupported spec type: " + std::string{spec_type});
}

// ---------------------------------------------------------------------------
// ContractSpec
// ---------------------------------------------------------------------------

namespace {

Result<xdr::SCVal> arg_via_typedef(const xdr::SCSpecTypeDef& type, const Json& j,
                                   const std::map<std::string, const xdr::SCSpecEntry*>& udts);

Result<xdr::SCVal> udt_scval(std::string_view name, const Json& j,
                             const std::map<std::string, const xdr::SCSpecEntry*>& udts) {
  const auto it = udts.find(std::string{name});
  if (it == udts.end()) {
    return Error::invalid_argument("unknown UDT: " + std::string{name});
  }
  const auto& e = *it->second;
  if (const auto* s = e.get_if_udtStructV0()) {
    // struct -> SCMap {symbol(field): value}
    if (!j.is_object()) {
      return Error::invalid_argument("UDT struct expects a JSON object");
    }
    std::vector<xdr::SCMapEntry> fields;
    for (const auto& f : s->fields) {
      const std::string fname{f.name.begin(), f.name.end()};
      const auto* val = j.find(fname);
      if (!val) {
        return Error::invalid_argument("missing struct field: " + fname);
      }
      STELLAR_TRY_ASSIGN(auto sv, arg_via_typedef(f.type, *val, udts));
      fields.push_back(
          xdr::SCMapEntry{xdr::Box<xdr::SCVal>(scval::symbol(fname)), xdr::Box<xdr::SCVal>(sv)});
    }
    return scval::map(std::move(fields));
  }
  if (const auto* un = e.get_if_udtUnionV0()) {
    // union -> {"variant": payload} or "variant" (void cases)
    std::string variant;
    Json payload;
    if (j.is_string()) {
      variant = j.as_string();
    } else if (j.is_object() && j.as_object().size() == 1) {
      variant = j.as_object().begin()->first;
      payload = j.as_object().begin()->second;
    } else {
      return Error::invalid_argument("UDT union expects a name or {name: payload}");
    }
    for (const auto& c : un->cases) {
      if (const auto* voidc = c.get_if_voidCase()) {
        const std::string cname{voidc->name.begin(), voidc->name.end()};
        if (cname == variant) return scval::vec({scval::symbol(cname)});
      } else if (const auto* tuple = c.get_if_tupleCase()) {
        const std::string cname{tuple->name.begin(), tuple->name.end()};
        if (cname != variant) continue;
        if (!payload.is_array() || payload.as_array().size() != tuple->type.size()) {
          return Error::invalid_argument("union case " + cname + " expects a JSON array");
        }
        std::vector<xdr::SCVal> elems{scval::symbol(cname)};
        for (std::size_t i = 0; i < tuple->type.size(); ++i) {
          STELLAR_TRY_ASSIGN(auto sv, arg_via_typedef(tuple->type[i], payload.as_array()[i], udts));
          elems.push_back(sv);
        }
        return scval::vec(std::move(elems));
      }
    }
    return Error::invalid_argument("unknown union variant: " + variant);
  }
  if (const auto* en = e.get_if_udtEnumV0()) {
    // enum -> u32 (name or number)
    for (const auto& c : en->cases) {
      const std::string cname{c.name.begin(), c.name.end()};
      if ((j.is_string() && j.as_string() == cname) || (j.is_number() && j.as_int() == c.value)) {
        return scval::u32(static_cast<std::uint32_t>(c.value));
      }
    }
    return Error::invalid_argument("unknown enum case for " + std::string{name});
  }
  if (const auto* er = e.get_if_udtErrorEnumV0()) {
    for (const auto& c : er->cases) {
      const std::string cname{c.name.begin(), c.name.end()};
      if ((j.is_string() && j.as_string() == cname) || (j.is_number() && j.as_int() == c.value)) {
        xdr::SCError err;
        err = xdr::SCError::sceContract(c.value);
        return xdr::SCVal::scvError(err);
      }
    }
    return Error::invalid_argument("unknown error enum case for " + std::string{name});
  }
  return Error::invalid_argument("UDT " + std::string{name} + " is not a type entry");
}

Result<xdr::SCVal> arg_via_typedef(const xdr::SCSpecTypeDef& type, const Json& j,
                                   const std::map<std::string, const xdr::SCSpecEntry*>& udts) {
  switch (type.type()) {
    case xdr::SCSpecType::SC_SPEC_TYPE_VAL:
      return json_to_scval(j);
    case xdr::SCSpecType::SC_SPEC_TYPE_BOOL:
      return j.is_bool() ? scval::bool_(j.as_bool()) : scval::bool_(j.as_string() == "true");
    case xdr::SCSpecType::SC_SPEC_TYPE_U32:
      return scval::u32(static_cast<std::uint32_t>(j.as_int()));
    case xdr::SCSpecType::SC_SPEC_TYPE_I32:
      return scval::i32(static_cast<std::int32_t>(j.as_int()));
    case xdr::SCSpecType::SC_SPEC_TYPE_U64:
      return scval::u64(static_cast<std::uint64_t>(j.as_int()));
    case xdr::SCSpecType::SC_SPEC_TYPE_I64:
      return scval::i64(j.as_int());
    case xdr::SCSpecType::SC_SPEC_TYPE_U128:
    case xdr::SCSpecType::SC_SPEC_TYPE_I128:
    case xdr::SCSpecType::SC_SPEC_TYPE_U256:
    case xdr::SCSpecType::SC_SPEC_TYPE_I256:
      return scval_from_string(xdr::enum_name(static_cast<xdr::SCSpecType>(type.type())),
                               j.as_string());
    case xdr::SCSpecType::SC_SPEC_TYPE_STRING:
      return scval::string(j.as_string());
    case xdr::SCSpecType::SC_SPEC_TYPE_SYMBOL:
      return scval::symbol(j.as_string());
    case xdr::SCSpecType::SC_SPEC_TYPE_ADDRESS: {
      auto a = Address::parse(j.as_string());
      if (!a.ok()) return a.error();
      return scval::address(*a);
    }
    case xdr::SCSpecType::SC_SPEC_TYPE_BYTES:
    case xdr::SCSpecType::SC_SPEC_TYPE_BYTES_N: {
      auto hb = hex_decode(j.as_string());
      if (!hb.ok()) return hb.error();
      return scval::bytes(ByteSpan(hb->data(), hb->size()));
    }
    case xdr::SCSpecType::SC_SPEC_TYPE_OPTION: {
      if (j.is_null()) return xdr::SCVal::scvVoid();
      STELLAR_TRY_ASSIGN(auto inner, arg_via_typedef(type.option().get().valueType.get(), j, udts));
      return scval::vec({std::move(inner)});  // Option -> [v] or []
    }
    case xdr::SCSpecType::SC_SPEC_TYPE_VEC: {
      if (!j.is_array()) return Error::invalid_argument("vec expects a JSON array");
      std::vector<xdr::SCVal> items;
      for (const auto& e : j.as_array()) {
        STELLAR_TRY_ASSIGN(auto sv, arg_via_typedef(type.vec().get().elementType.get(), e, udts));
        items.push_back(std::move(sv));
      }
      return scval::vec(std::move(items));
    }
    case xdr::SCSpecType::SC_SPEC_TYPE_MAP: {
      if (!j.is_object()) return Error::invalid_argument("map expects a JSON object");
      std::vector<xdr::SCMapEntry> entries;
      for (const auto& [k, v] : j.as_object()) {
        STELLAR_TRY_ASSIGN(auto kv, arg_via_typedef(type.map().get().keyType.get(), Json(k), udts));
        STELLAR_TRY_ASSIGN(auto vv, arg_via_typedef(type.map().get().valueType.get(), v, udts));
        entries.push_back(xdr::SCMapEntry{xdr::Box<xdr::SCVal>(kv), xdr::Box<xdr::SCVal>(vv)});
      }
      return scval::map(std::move(entries));
    }
    case xdr::SCSpecType::SC_SPEC_TYPE_TUPLE: {
      if (!j.is_array() || j.as_array().size() != type.tuple().get().valueTypes->size()) {
        return Error::invalid_argument("tuple expects a fixed-length array");
      }
      std::vector<xdr::SCVal> elems;
      for (std::size_t i = 0; i < type.tuple().get().valueTypes->size(); ++i) {
        STELLAR_TRY_ASSIGN(
            auto sv, arg_via_typedef((*type.tuple().get().valueTypes)[i], j.as_array()[i], udts));
        elems.push_back(std::move(sv));
      }
      return scval::vec(std::move(elems));
    }
    case xdr::SCSpecType::SC_SPEC_TYPE_UDT:
      return udt_scval(type.udt().name, j, udts);
    default:
      return Error::invalid_argument("unsupported spec type for conversion");
  }
}

}  // namespace

Result<ContractSpec> ContractSpec::decode(ByteSpan spec_xdr) {
  ContractSpec out;
  xdr::Decoder d(spec_xdr, xdr::Limits{});
  while (d.remaining() > 0) {
    xdr::SCSpecEntry e;
    if (!xdr::Codec<xdr::SCSpecEntry>::decode(e, d)) {
      return d.error();
    }
    out.entries_.push_back(std::move(e));
  }
  for (const auto& e : out.entries_) {
    if (const auto* f = e.get_if_functionV0()) {
      out.functions_[std::string{f->name}] = *f;
    } else {
      for (const char* kind : {"udtStructV0", "udtUnionV0", "udtEnumV0", "udtErrorEnumV0"}) {
        (void)kind;
        break;  // handled below
      }
      if (const auto* s = e.get_if_udtStructV0()) {
        out.udts_[std::string{s->name}] = &e;
      } else if (const auto* u = e.get_if_udtUnionV0()) {
        out.udts_[std::string{u->name}] = &e;
      } else if (const auto* en = e.get_if_udtEnumV0()) {
        out.udts_[std::string{en->name}] = &e;
      } else if (const auto* er = e.get_if_udtErrorEnumV0()) {
        out.udts_[std::string{er->name}] = &e;
      }
    }
  }
  return out;
}

const xdr::SCSpecFunctionV0* ContractSpec::function(std::string_view name) const {
  const auto it = functions_.find(std::string{name});
  return it != functions_.end() ? &it->second : nullptr;
}

Result<xdr::SCVal> ContractSpec::arg_to_scval(const xdr::SCSpecTypeDef& type,
                                              const Json& arg) const {
  return arg_via_typedef(type, arg, udts_);
}

Result<std::vector<xdr::SCVal>> ContractSpec::args_from_json(std::string_view function_name,
                                                             const Json& args) const {
  const auto* fn = function(function_name);
  if (!fn) {
    return Error::invalid_argument("unknown function: " + std::string{function_name});
  }
  std::vector<xdr::SCVal> out;
  if (args.is_object()) {
    // named args
    for (const auto& input : fn->inputs) {
      const std::string iname{input.name.begin(), input.name.end()};
      const auto* jv = args.find(iname);
      if (!jv) {
        return Error::invalid_argument("missing arg: " + iname);
      }
      STELLAR_TRY_ASSIGN(auto v, arg_to_scval(input.type, *jv));
      out.push_back(std::move(v));
    }
  } else if (args.is_array()) {
    if (args.as_array().size() != fn->inputs.size()) {
      return Error::invalid_argument("arg count mismatch for " + std::string{function_name});
    }
    for (std::size_t i = 0; i < fn->inputs.size(); ++i) {
      STELLAR_TRY_ASSIGN(auto v, arg_to_scval(fn->inputs[i].type, args.as_array()[i]));
      out.push_back(std::move(v));
    }
  } else {
    return Error::invalid_argument("args must be an object or array");
  }
  return out;
}

}  // namespace stellar::inline v1
