# Overlay port for stellar-cpp-sdk.
#
# CI and local development set STELLAR_SDK_LOCAL_SOURCE to a checked-out tree
# so the port builds exactly the code under test; drop that branch when
# submitting to the vcpkg registry.
if(DEFINED ENV{STELLAR_SDK_LOCAL_SOURCE})
  set(SOURCE_PATH "$ENV{STELLAR_SDK_LOCAL_SOURCE}")
else()
  vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Olvisgil/stellar-cpp-sdk
    REF "v${VERSION}"
    SHA512 b9597d0890b690ab88763a4e739e3c3853dec815cfbd196c23c6abb64f4e0a0a8a3bfa1792931a27ffd3bec6c6440c09992ba6d2468a7ee5b69566c2ab22b85d
    HEAD_REF main
  )
endif()

# The SDK builds STATIC libraries only: generated XDR symbols carry no export
# annotations and the build hides visibility by default.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS
    -DSTELLAR_SDK_BUILD_TESTS=OFF
    -DSTELLAR_SDK_BUILD_TOOLS=OFF
    -DSTELLAR_SDK_BUILD_EXAMPLES=OFF
    -DSTELLAR_SDK_BUILD_FUZZERS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME StellarSdk CONFIG_PATH lib/cmake/StellarSdk)
vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
