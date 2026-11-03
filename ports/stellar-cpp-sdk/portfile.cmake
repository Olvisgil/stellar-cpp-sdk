# Overlay port for stellar-cpp-sdk.
#
# CI and local development set STELLAR_SDK_LOCAL_SOURCE to a checked-out tree
# so the port builds exactly the code under test. Once a release tag exists
# that contains the port-ready CMake config, switch the else-branch to
# vcpkg_from_github(REF "v${VERSION}" SHA512 ...) for registry submission.
if(DEFINED ENV{STELLAR_SDK_LOCAL_SOURCE})
  set(SOURCE_PATH "$ENV{STELLAR_SDK_LOCAL_SOURCE}")
else()
  vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/Olvisgil/stellar-cpp-sdk.git
    REF main
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
