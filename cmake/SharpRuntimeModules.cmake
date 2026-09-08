# SPDX-License-Identifier: MIT
# Copyright (c) Robert Vokac and contributors

include_guard(GLOBAL)

set(SHARP_RUNTIME_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH SHARP_RUNTIME_ROOT)
string(REGEX REPLACE "/$" "" SHARP_RUNTIME_ROOT "${SHARP_RUNTIME_ROOT}")

include("${CMAKE_CURRENT_LIST_DIR}/SharpRuntimeCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SharpRuntimeComponents.cmake")

set(sharp_runtime_module_directories
    core
    console
    uri
    time-zone
    buffers
    collections
    collections-blocking
    collections-async
    collections-object-model
    component-model
    diagnostics
    globalization
    resources
    numerics
    runtime
    text
    text-json
    text-regular-expressions
    threading
    threading-tasks
    threading-channels
    timers
    io
    io-compression
    io-compression-zip
    io-hashing
    storage
    io-isolated-storage
    net
    net-sockets
    net-http
    net-http-headers
    net-http-json
    net-mime
    net-network-information
    net-security
    net-websockets
    security
    security-cryptography
    security-cryptography-random
    xml
    xml-linq
    xml-serialization
    service-model
)

foreach(module_directory IN LISTS sharp_runtime_module_directories)
    add_subdirectory(
        "${SHARP_RUNTIME_ROOT}/modules/${module_directory}"
        "${CMAKE_CURRENT_BINARY_DIR}/sharp-runtime-modules/${module_directory}"
    )
endforeach()

sharp_runtime_validate_source_partition()
