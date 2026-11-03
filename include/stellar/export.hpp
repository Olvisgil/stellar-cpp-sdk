// Copyright 2026 The stellar-cpp-sdk Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file export.hpp
/// Symbol visibility for shared-library builds.
///
/// The SDK ships as static libraries, so STELLAR_API is empty unless a shared
/// build is explicitly requested: STELLAR_SDK_BUILD_SHARED while compiling the
/// library, STELLAR_SDK_SHARED when consuming the shared build.

#if defined(_WIN32) || defined(__CYGWIN__)
#define STELLAR_EXPORT_IMPORT __declspec(dllexport)
#define STELLAR_IMPORT_IMPORT __declspec(dllimport)
#else
#define STELLAR_EXPORT_IMPORT __attribute__((visibility("default")))
#define STELLAR_IMPORT_IMPORT
#endif

#if defined(STELLAR_SDK_BUILD_SHARED)
#define STELLAR_API STELLAR_EXPORT_IMPORT
#elif defined(STELLAR_SDK_SHARED)
#define STELLAR_API STELLAR_IMPORT_IMPORT
#else
#define STELLAR_API
#endif
