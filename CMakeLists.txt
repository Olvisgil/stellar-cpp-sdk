cmake_minimum_required(VERSION 3.24)
project(stellar-cpp-sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

option(STELLAR_SDK_BUILD_TOOLS "Build CLI tools (stellar-xdr-cpp)" ON)
option(STELLAR_SDK_BUILD_TESTS "Build tests" ON)
option(STELLAR_SDK_BUILD_EXAMPLES "Build examples" ON)
option(STELLAR_SDK_BUILD_FUZZERS "Build libFuzzer targets (Clang)" OFF)
option(STELLAR_SDK_ENABLE_SANITIZERS "Enable ASan+UBSan" OFF)
option(STELLAR_SDK_ENABLE_TSAN "Enable TSan" OFF)
option(STELLAR_SDK_WERROR "Warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)

# ---------------------------------------------------------------------------
# stellar::base — Result/Error + encoding utilities (no deps)
# ---------------------------------------------------------------------------
# static builds must not emit dllexport/dllimport attributes
if(NOT BUILD_SHARED_LIBS)
  add_compile_definitions(STELLAR_SDK_STATIC_DEFINE)
endif()

add_library(stellar_base STATIC
  src/util/error.cpp
  src/util/encoding.cpp
)
add_library(stellar::base ALIAS stellar_base)
target_compile_features(stellar_base PUBLIC cxx_std_20)
target_include_directories(stellar_base
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_compile_definitions(stellar_base PRIVATE STELLAR_SDK_BUILD)
stellar_set_warnings(stellar_base)

# ---------------------------------------------------------------------------
# stellar::xdr — generated protocol types + strict codec (no deps)
# ---------------------------------------------------------------------------
file(GLOB XDR_GENERATED_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/xdr/generated/*.cpp")
add_library(stellar_xdr STATIC ${XDR_GENERATED_SOURCES})
if(MSVC)
  # generated tables exceed the 64k-section COFF limit
  target_compile_options(stellar_xdr PRIVATE /bigobj)
endif()
add_library(stellar::xdr ALIAS stellar_xdr)
target_compile_features(stellar_xdr PUBLIC cxx_std_20)
target_link_libraries(stellar_xdr PUBLIC stellar::base)
stellar_set_warnings(stellar_xdr)
# Generated code is noisy; keep warnings non-fatal for it.
target_compile_options(stellar_xdr PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wno-unused-parameter>)

# ---------------------------------------------------------------------------
# stellar::core — keys, strkeys, networks, numerics, addresses (libsodium private)
# ---------------------------------------------------------------------------
# libsodium: vcpkg ships it as unofficial-sodium; system installs are found by
# path. Either way we normalize to the imported target `stellar_sodium`, which
# StellarSdkConfig.cmake re-creates for consumers (static PRIVATE deps are
# exported as $<LINK_ONLY:...>).
find_package(unofficial-sodium CONFIG QUIET)
if(TARGET unofficial-sodium::sodium)
  add_library(stellar_sodium INTERFACE IMPORTED)
  target_link_libraries(stellar_sodium INTERFACE unofficial-sodium::sodium)
else()
  find_library(SODIUM_LIBRARY NAMES sodium libsodium REQUIRED)
  find_path(SODIUM_INCLUDE_DIR sodium.h REQUIRED)
  add_library(stellar_sodium UNKNOWN IMPORTED)
  set_target_properties(stellar_sodium PROPERTIES
    IMPORTED_LOCATION "${SODIUM_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}")
endif()
find_package(CURL REQUIRED)

add_library(stellar_core STATIC
  src/core/address.cpp
  src/core/amount.cpp
  src/core/asset.cpp
  src/core/bigint.cpp
  src/core/bip39_english.cpp
  src/core/keypair.cpp
  src/core/memo.cpp
  src/core/message_signing.cpp
  src/core/mnemonic.cpp
  src/core/network.cpp
  src/core/price.cpp
  src/core/strkey.cpp
  src/contract/contract_client.cpp
  src/contract/contract_meta.cpp
  src/contract/delegate_tree.cpp
  src/contract/scval.cpp
  src/contract/token_client.cpp
  src/util/clock.cpp
  src/http/curl_transport.cpp
  src/http/http.cpp
  src/json/json.cpp
  src/rpc/rpc_client.cpp
  src/tx/ops.cpp
  src/tx/transaction.cpp
)
add_library(stellar::core ALIAS stellar_core)
target_compile_features(stellar_core PUBLIC cxx_std_20)
target_link_libraries(stellar_core PUBLIC stellar::xdr PRIVATE stellar_sodium CURL::libcurl)
target_compile_definitions(stellar_core PRIVATE STELLAR_SDK_BUILD)
stellar_set_warnings(stellar_core)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(STELLAR_SDK_BUILD_TOOLS)
  add_executable(stellar-xdr-cpp tools/stellar_xdr_cli.cpp)
  target_link_libraries(stellar-xdr-cpp PRIVATE stellar::xdr)
  stellar_set_warnings(stellar-xdr-cpp)
endif()

# ---------------------------------------------------------------------------
# codegen drift check: `cmake --build build --target check-generated`
# ---------------------------------------------------------------------------
add_custom_target(check-generated
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/xdrgen/generate.py --check
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Verifying generated XDR code is up to date")
add_custom_target(check-xdr-pins
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/update_xdr.py --check
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Verifying vendored XDR inputs match xdr/VERSION")

if(STELLAR_SDK_ENABLE_SANITIZERS)
  foreach(t stellar_base stellar_xdr)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${t} PUBLIC -fsanitize=address,undefined)
  endforeach()
endif()

if(STELLAR_SDK_ENABLE_TSAN)
  foreach(t stellar_base stellar_xdr)
    target_compile_options(${t} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${t} PUBLIC -fsanitize=thread)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(STELLAR_SDK_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    FIND_PACKAGE_ARGS NAMES GTest)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(STELLAR_SDK_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
  add_subdirectory(examples)
endif()


# ---------------------------------------------------------------------------
# Install / export — component-wise (find_package(StellarSdk COMPONENTS core …))
# ---------------------------------------------------------------------------

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_sdk_components base xdr core)
foreach(comp IN LISTS _sdk_components)
  # Installed names match the build-tree aliases (stellar::base, stellar::xdr,
  # stellar::core) rather than stellar::stellar_<comp>.
  set_target_properties(stellar_${comp} PROPERTIES EXPORT_NAME ${comp})
  install(TARGETS stellar_${comp}
          EXPORT StellarSdkTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
          INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endforeach()

install(DIRECTORY include/stellar DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT StellarSdkTargets
        FILE StellarSdkTargets.cmake
        NAMESPACE stellar::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StellarSdk)

configure_package_config_file(
    cmake/StellarSdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/StellarSdkConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StellarSdk)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/StellarSdkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/StellarSdkConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/StellarSdkConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StellarSdk)
