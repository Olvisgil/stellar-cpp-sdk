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
    SHA512 fe054c70fe297d5251f2cbcaa8a30633ba0b9a47c81c892c052dabf85c2c4840c1fbcf6d88a2f4f469bb98997a8d90f2a7440ae0ba8faaae60bfd0a7579473be
    HEAD_REF main
  )
endif()

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
