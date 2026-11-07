# Copyright 2026 The stellar-cpp-sdk Authors
# SPDX-License-Identifier: Apache-2.0

add_executable(stellar_tests
  unit/encoding_test.cpp
  unit/xdr_test.cpp
  unit/xdr_fixture_test.cpp
  unit/core_test.cpp
  unit/core2_test.cpp
  unit/tx_test.cpp
  unit/rpc_test.cpp
  unit/contract_test.cpp
  unit/cross_sdk_test.cpp
  integration/rpc_live_test.cpp
)
target_link_libraries(stellar_tests PRIVATE stellar::xdr stellar::core GTest::gtest_main GTest::gmock)
target_compile_definitions(stellar_tests PRIVATE
  STELLAR_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xdr")
stellar_set_warnings(stellar_tests)
if(TARGET stellar_iso20022)
  target_sources(stellar_tests PRIVATE
    unit/iso20022_test.cpp
    integration/iso20022_e2e_test.cpp)
  target_link_libraries(stellar_tests PRIVATE stellar::iso20022)
  target_compile_definitions(stellar_tests PRIVATE
    STELLAR_ISO20022_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/iso20022")
endif()
include(GoogleTest)
gtest_discover_tests(stellar_tests)

if(STELLAR_SDK_BUILD_FUZZERS AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(TARGET stellar_iso20022)
    add_executable(fuzz_iso20022_parse fuzz/fuzz_iso20022_parse.cpp)
    target_link_libraries(fuzz_iso20022_parse PRIVATE stellar::iso20022)
    target_compile_options(fuzz_iso20022_parse PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_iso20022_parse PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()

  add_executable(fuzz_xdr_decode fuzz/fuzz_decode.cpp)
  target_link_libraries(fuzz_xdr_decode PRIVATE stellar::xdr)
  target_compile_options(fuzz_xdr_decode PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_xdr_decode PRIVATE -fsanitize=fuzzer,address,undefined)

  # one target per top-level network-facing type
  set(STELLAR_FUZZ_TYPES
    TransactionEnvelope TransactionResult TransactionMeta TransactionMetaV4
    LedgerCloseMeta SCVal SCMapEntry SorobanTransactionData
    TransactionSignaturePayload LedgerEntry StellarMessage
    TransactionSet GeneralizedTransactionSet SorobanAuthorizationEntry
    OperationResult)
  foreach(t IN LISTS STELLAR_FUZZ_TYPES)
    add_executable(fuzz_${t} fuzz/fuzz_decode_type.cpp)
    target_link_libraries(fuzz_${t} PRIVATE stellar::xdr)
    target_compile_definitions(fuzz_${t} PRIVATE FUZZ_XDR_TYPE=${t})
    target_compile_options(fuzz_${t} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()
