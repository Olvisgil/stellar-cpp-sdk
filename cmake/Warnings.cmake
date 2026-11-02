# Copyright 2026 The stellar-cpp-sdk Authors
# SPDX-License-Identifier: Apache-2.0
# Warning configuration shared by all SDK targets.

function(stellar_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    if(STELLAR_SDK_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wconversion -Wsign-conversion
      -Wcast-qual -Wformat=2
      -Wundef -Wshadow
      -Wnull-dereference
      -Wdouble-promotion)
    if(STELLAR_SDK_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
