cmake_minimum_required(VERSION 3.20)

project(RimauWebServer
    VERSION 0.1.0
    DESCRIPTION "Rimau Web Server - C++ web server scaffold targeting HTTP/1.1, HTTP/2, and HTTP/3"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RIMAU_ENABLE_TESTS "Build Rimau unit tests" ON)
option(RIMAU_USE_BUNDLED_SQLITE "Build and link Rimau against bundled static SQLite" ON)
option(RIMAU_USE_BUNDLED_OPENSSL "Build and link Rimau against bundled static OpenSSL" ON)
option(RIMAU_USE_BUNDLED_ZLIB "Build and link Rimau against bundled static zlib" ON)
option(RIMAU_FULLY_STATIC_SERVER "Link rimau-server as a fully static Linux ELF when supported" ON)
option(RIMAU_USE_BUNDLED_GLIBC "Build glibc from original source and link rimau-server against that sysroot" ON)
option(RIMAU_STATIC_CXX_RUNTIME "Link rimau-server with static libstdc++ and libgcc when supported" ON)

find_package(Threads REQUIRED)

if(NOT RIMAU_USE_BUNDLED_SQLITE)
    message(FATAL_ERROR "System SQLite is intentionally disabled. Build with bundled SQLite.")
endif()

if(NOT RIMAU_USE_BUNDLED_OPENSSL)
    message(FATAL_ERROR "System OpenSSL is intentionally disabled. Build with bundled OpenSSL.")
endif()

if(NOT RIMAU_USE_BUNDLED_ZLIB)
    message(FATAL_ERROR "System zlib is intentionally disabled. Build with bundled zlib.")
endif()

if(RIMAU_FULLY_STATIC_SERVER AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    message(FATAL_ERROR "RIMAU_FULLY_STATIC_SERVER currently supports Linux GNU/Clang toolchains only.")
endif()

if(RIMAU_USE_BUNDLED_GLIBC AND NOT RIMAU_FULLY_STATIC_SERVER)
    message(FATAL_ERROR "Bundled glibc source builds are only supported for the fully static rimau-server target.")
endif()

if(RIMAU_USE_BUNDLED_GLIBC AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"))
    message(FATAL_ERROR "Bundled glibc source build is currently wired for Linux x86_64 only.")
endif()

include(ExternalProject)

find_program(RIMAU_PERL_EXECUTABLE perl REQUIRED)
find_program(RIMAU_MAKE_EXECUTABLE NAMES make gmake REQUIRED)
find_program(RIMAU_GAWK_EXECUTABLE gawk REQUIRED)
find_program(RIMAU_MSGFMT_EXECUTABLE msgfmt REQUIRED)
find_program(RIMAU_PYTHON3_EXECUTABLE python3 REQUIRED)

set(RIMAU_OPENSSL_TAG "openssl-4.0.1" CACHE STRING "Bundled OpenSSL GitHub release tag")
set(RIMAU_OPENSSL_SHA256 "2db3f3a0d6ea4b59e1f094ace2c8cd536dffb87cdc39084c5afa1e6f7f37dd09" CACHE STRING "Bundled OpenSSL release SHA256")
set(RIMAU_OPENSSL_URL "https://github.com/openssl/openssl/releases/download/${RIMAU_OPENSSL_TAG}/${RIMAU_OPENSSL_TAG}.tar.gz" CACHE STRING "Bundled OpenSSL source URL")
set(RIMAU_OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/openssl/install")

set(RIMAU_SQLITE_VERSION "3.53.3" CACHE STRING "Bundled SQLite release version")
set(RIMAU_SQLITE_RELEASE_ID "3530300" CACHE STRING "Bundled SQLite numeric release id")
set(RIMAU_SQLITE_SHA256 "646421e12aac110282ef8cc68f1a62d4bb15fc7b8f09da0b53e29ee690500431" CACHE STRING "Bundled SQLite amalgamation ZIP SHA256")
set(RIMAU_SQLITE_URL "https://sqlite.org/2026/sqlite-amalgamation-${RIMAU_SQLITE_RELEASE_ID}.zip" CACHE STRING "Bundled SQLite amalgamation URL")
set(RIMAU_SQLITE_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/sqlite/install")

set(RIMAU_ZLIB_VERSION "1.3.2" CACHE STRING "Bundled zlib release version")
set(RIMAU_ZLIB_SHA256 "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16" CACHE STRING "Bundled zlib release SHA256")
set(RIMAU_ZLIB_URL "https://github.com/madler/zlib/releases/download/v${RIMAU_ZLIB_VERSION}/zlib-${RIMAU_ZLIB_VERSION}.tar.gz" CACHE STRING "Bundled zlib source URL")
set(RIMAU_ZLIB_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/zlib/install")

set(RIMAU_BISON_VERSION "3.8.2" CACHE STRING "Bundled GNU Bison build-tool version")
set(RIMAU_BISON_SHA256 "9bba0214ccf7f1079c5d59210045227bcf619519840ebfa80cd3849cff5a5bf2" CACHE STRING "Bundled GNU Bison release SHA256")
set(RIMAU_BISON_URL "https://ftp.gnu.org/gnu/bison/bison-${RIMAU_BISON_VERSION}.tar.xz" CACHE STRING "Bundled GNU Bison source URL")
set(RIMAU_BISON_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/bison/install")

set(RIMAU_GLIBC_VERSION "2.43" CACHE STRING "Bundled glibc release version")
set(RIMAU_GLIBC_SHA256 "d9c86c6b5dbddb43a3e08270c5844fc5177d19442cf5b8df4be7c07cd5fa3831" CACHE STRING "Bundled glibc release SHA256")
set(RIMAU_GLIBC_URL "https://ftp.gnu.org/gnu/glibc/glibc-${RIMAU_GLIBC_VERSION}.tar.xz" CACHE STRING "Bundled glibc source URL")
set(RIMAU_GLIBC_SYSROOT "${CMAKE_BINARY_DIR}/_deps/glibc/sysroot")
set(RIMAU_GLIBC_LIB_DIR "${RIMAU_GLIBC_SYSROOT}/usr/lib")

set(RIMAU_LINUX_HEADERS_VERSION "6.18.7" CACHE STRING "Bundled Linux kernel headers source version")
set(RIMAU_LINUX_HEADERS_SHA256 "b726a4d15cf9ae06219b56d87820776e34d89fbc137e55fb54a9b9c3015b8f1e" CACHE STRING "Bundled Linux kernel source SHA256")
set(RIMAU_LINUX_HEADERS_URL "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${RIMAU_LINUX_HEADERS_VERSION}.tar.xz" CACHE STRING "Bundled Linux kernel source URL for UAPI headers")
set(RIMAU_LINUX_HEADERS_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/linux-headers/install")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(RIMAU_OPENSSL_CONFIG_TARGET "linux-x86_64" CACHE STRING "OpenSSL Configure target")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(RIMAU_OPENSSL_CONFIG_TARGET "linux-aarch64" CACHE STRING "OpenSSL Configure target")
else()
    message(FATAL_ERROR "Bundled OpenSSL configure target is not mapped for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()

file(MAKE_DIRECTORY "${RIMAU_OPENSSL_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${RIMAU_SQLITE_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${RIMAU_ZLIB_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${RIMAU_BISON_INSTALL_DIR}/bin")
file(MAKE_DIRECTORY "${RIMAU_LINUX_HEADERS_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${RIMAU_GLIBC_LIB_DIR}")

ExternalProject_Add(rimau_bundled_openssl
    URL "${RIMAU_OPENSSL_URL}"
    URL_HASH "SHA256=${RIMAU_OPENSSL_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/openssl/src"
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND
        "${RIMAU_PERL_EXECUTABLE}" Configure
            "${RIMAU_OPENSSL_CONFIG_TARGET}"
            no-shared
            no-dso
            no-tests
            no-docs
            --libdir=lib
            "--prefix=${RIMAU_OPENSSL_INSTALL_DIR}"
            "--openssldir=${RIMAU_OPENSSL_INSTALL_DIR}/ssl"
    BUILD_COMMAND "${RIMAU_MAKE_EXECUTABLE}"
    INSTALL_COMMAND "${RIMAU_MAKE_EXECUTABLE}" install_sw
    BUILD_BYPRODUCTS
        "${RIMAU_OPENSSL_INSTALL_DIR}/lib/libssl.a"
        "${RIMAU_OPENSSL_INSTALL_DIR}/lib/libcrypto.a"
        "${RIMAU_OPENSSL_INSTALL_DIR}/bin/openssl")

ExternalProject_Add(rimau_bundled_sqlite
    URL "${RIMAU_SQLITE_URL}"
    URL_HASH "SHA256=${RIMAU_SQLITE_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/sqlite/src"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/sqlite/build"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
        "${CMAKE_COMMAND}" -E make_directory
            "${RIMAU_SQLITE_INSTALL_DIR}/lib"
            "${RIMAU_SQLITE_INSTALL_DIR}/include"
        COMMAND
            "${CMAKE_C_COMPILER}"
            -O2
            -fPIC
            -DSQLITE_THREADSAFE=1
            -DSQLITE_OMIT_LOAD_EXTENSION=1
            -DSQLITE_DEFAULT_MEMSTATUS=0
            -DSQLITE_DEFAULT_FOREIGN_KEYS=1
            -c <SOURCE_DIR>/sqlite3.c
            -o <BINARY_DIR>/sqlite3.o
        COMMAND
            "${CMAKE_AR}" qc
            "${RIMAU_SQLITE_INSTALL_DIR}/lib/libsqlite3.a"
            <BINARY_DIR>/sqlite3.o
        COMMAND
            "${CMAKE_RANLIB}"
            "${RIMAU_SQLITE_INSTALL_DIR}/lib/libsqlite3.a"
    INSTALL_COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
            <SOURCE_DIR>/sqlite3.h
            "${RIMAU_SQLITE_INSTALL_DIR}/include/sqlite3.h"
        COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
            <SOURCE_DIR>/sqlite3ext.h
            "${RIMAU_SQLITE_INSTALL_DIR}/include/sqlite3ext.h"
    BUILD_BYPRODUCTS
        "${RIMAU_SQLITE_INSTALL_DIR}/lib/libsqlite3.a"
        "${RIMAU_SQLITE_INSTALL_DIR}/include/sqlite3.h")

ExternalProject_Add(rimau_bundled_zlib
    URL "${RIMAU_ZLIB_URL}"
    URL_HASH "SHA256=${RIMAU_ZLIB_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/zlib/src"
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND
        <SOURCE_DIR>/configure
            --static
            "--prefix=${RIMAU_ZLIB_INSTALL_DIR}"
    BUILD_COMMAND "${RIMAU_MAKE_EXECUTABLE}"
    INSTALL_COMMAND "${RIMAU_MAKE_EXECUTABLE}" install
    BUILD_BYPRODUCTS
        "${RIMAU_ZLIB_INSTALL_DIR}/lib/libz.a")

ExternalProject_Add(rimau_bundled_bison
    EXCLUDE_FROM_ALL TRUE
    URL "${RIMAU_BISON_URL}"
    URL_HASH "SHA256=${RIMAU_BISON_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/bison/src"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/bison/build"
    CONFIGURE_COMMAND
        <SOURCE_DIR>/configure
            "--prefix=${RIMAU_BISON_INSTALL_DIR}"
    BUILD_COMMAND "${RIMAU_MAKE_EXECUTABLE}"
    INSTALL_COMMAND "${RIMAU_MAKE_EXECUTABLE}" install
    BUILD_BYPRODUCTS
        "${RIMAU_BISON_INSTALL_DIR}/bin/bison")

ExternalProject_Add(rimau_bundled_linux_headers
    EXCLUDE_FROM_ALL TRUE
    URL "${RIMAU_LINUX_HEADERS_URL}"
    URL_HASH "SHA256=${RIMAU_LINUX_HEADERS_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/linux-headers/src"
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
        "${RIMAU_MAKE_EXECUTABLE}"
            ARCH=x86_64
            headers_install
            "INSTALL_HDR_PATH=${RIMAU_LINUX_HEADERS_INSTALL_DIR}"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
        "${RIMAU_LINUX_HEADERS_INSTALL_DIR}/include/asm/errno.h"
        "${RIMAU_LINUX_HEADERS_INSTALL_DIR}/include/asm/prctl.h"
        "${RIMAU_LINUX_HEADERS_INSTALL_DIR}/include/linux/errno.h")

ExternalProject_Add(rimau_bundled_glibc
    EXCLUDE_FROM_ALL TRUE
    URL "${RIMAU_GLIBC_URL}"
    URL_HASH "SHA256=${RIMAU_GLIBC_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/glibc/src"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/glibc/build"
    CONFIGURE_COMMAND
        "${CMAKE_COMMAND}" -E env
            "BISON=${RIMAU_BISON_INSTALL_DIR}/bin/bison"
            "AWK=${RIMAU_GAWK_EXECUTABLE}"
            "MSGFMT=${RIMAU_MSGFMT_EXECUTABLE}"
            "PYTHON=${RIMAU_PYTHON3_EXECUTABLE}"
            <SOURCE_DIR>/configure
                --prefix=/usr
                --disable-werror
                --without-selinux
                --enable-kernel=3.2.0
                "--with-headers=${RIMAU_LINUX_HEADERS_INSTALL_DIR}/include"
                libc_cv_slibdir=/usr/lib
    BUILD_COMMAND
        "${CMAKE_COMMAND}" -E env
            "BISON=${RIMAU_BISON_INSTALL_DIR}/bin/bison"
            "AWK=${RIMAU_GAWK_EXECUTABLE}"
            "MSGFMT=${RIMAU_MSGFMT_EXECUTABLE}"
            "PYTHON=${RIMAU_PYTHON3_EXECUTABLE}"
            "${RIMAU_MAKE_EXECUTABLE}"
    INSTALL_COMMAND
        "${CMAKE_COMMAND}" -E env
            "BISON=${RIMAU_BISON_INSTALL_DIR}/bin/bison"
            "AWK=${RIMAU_GAWK_EXECUTABLE}"
            "MSGFMT=${RIMAU_MSGFMT_EXECUTABLE}"
            "PYTHON=${RIMAU_PYTHON3_EXECUTABLE}"
            "${RIMAU_MAKE_EXECUTABLE}" install
            "DESTDIR=${RIMAU_GLIBC_SYSROOT}"
    BUILD_BYPRODUCTS
        "${RIMAU_GLIBC_LIB_DIR}/libc.a"
        "${RIMAU_GLIBC_LIB_DIR}/crt1.o"
        "${RIMAU_GLIBC_LIB_DIR}/crti.o"
        "${RIMAU_GLIBC_LIB_DIR}/crtn.o")

add_dependencies(rimau_bundled_glibc rimau_bundled_bison rimau_bundled_linux_headers)

add_library(RimauBundledOpenSSL::SSL STATIC IMPORTED GLOBAL)
set_target_properties(RimauBundledOpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${RIMAU_OPENSSL_INSTALL_DIR}/lib/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RIMAU_OPENSSL_INSTALL_DIR}/include")

add_library(RimauBundledOpenSSL::Crypto STATIC IMPORTED GLOBAL)
set_target_properties(RimauBundledOpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${RIMAU_OPENSSL_INSTALL_DIR}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RIMAU_OPENSSL_INSTALL_DIR}/include")

add_library(RimauBundledSQLite::SQLite3 STATIC IMPORTED GLOBAL)
set_target_properties(RimauBundledSQLite::SQLite3 PROPERTIES
    IMPORTED_LOCATION "${RIMAU_SQLITE_INSTALL_DIR}/lib/libsqlite3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RIMAU_SQLITE_INSTALL_DIR}/include")

add_library(RimauBundledZLIB::ZLIB STATIC IMPORTED GLOBAL)
set_target_properties(RimauBundledZLIB::ZLIB PROPERTIES
    IMPORTED_LOCATION "${RIMAU_ZLIB_INSTALL_DIR}/lib/libz.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RIMAU_ZLIB_INSTALL_DIR}/include")

add_library(rimau_core
    src/core/config.cpp
    src/core/logger.cpp
    src/core/server.cpp
    src/http/http1_session.cpp
    src/http/parser.cpp
    src/http/response_builder.cpp
    src/http/response.cpp
    src/http/response_sink.cpp
    src/http/static_file_handler.cpp
    src/http/transaction.cpp
    src/http/virtual_host.cpp
    src/http/waf.cpp
    src/protocol/http2_frame.cpp
    src/protocol/http2_gateway.cpp
    src/protocol/http2_hpack.cpp
    src/protocol/http3_frame.cpp
    src/protocol/http3_gateway.cpp
    src/protocol/protocol.cpp)

target_include_directories(rimau_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(rimau_core
    PUBLIC
        Threads::Threads
        RimauBundledSQLite::SQLite3
        RimauBundledZLIB::ZLIB
        RimauBundledOpenSSL::SSL
        RimauBundledOpenSSL::Crypto
        ${CMAKE_DL_LIBS})

add_dependencies(rimau_core rimau_bundled_openssl rimau_bundled_sqlite rimau_bundled_zlib)

target_compile_features(rimau_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(rimau_core PRIVATE /W4)
else()
    target_compile_options(rimau_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rimau-server src/main.cpp)
target_link_libraries(rimau-server PRIVATE rimau_core)

if(RIMAU_FULLY_STATIC_SERVER)
    if(RIMAU_USE_BUNDLED_GLIBC)
        target_link_options(rimau-server PRIVATE
            -static
            "-B${RIMAU_GLIBC_LIB_DIR}/"
            "--sysroot=${RIMAU_GLIBC_SYSROOT}"
            "-L${RIMAU_GLIBC_LIB_DIR}"
            "-Wl,-rpath-link,${RIMAU_GLIBC_LIB_DIR}")
        add_dependencies(rimau-server rimau_bundled_glibc)
    else()
        target_link_options(rimau-server PRIVATE -static)
    endif()
elseif(RIMAU_STATIC_CXX_RUNTIME AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(rimau-server PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(RIMAU_ENABLE_TESTS)
    enable_testing()

    set(RIMAU_EXPECT_FULLY_STATIC_TEST false)
    if(RIMAU_FULLY_STATIC_SERVER AND RIMAU_USE_BUNDLED_GLIBC)
        set(RIMAU_EXPECT_FULLY_STATIC_TEST true)
    endif()

    add_executable(rimau-http-parser-tests
        tests/test_http_parser.cpp)
    target_link_libraries(rimau-http-parser-tests PRIVATE rimau_core)

    add_executable(rimau-handler-pipeline-tests
        tests/test_handler_pipeline.cpp)
    target_link_libraries(rimau-handler-pipeline-tests PRIVATE rimau_core)

    add_executable(rimau-http-response-tests
        tests/test_http_response.cpp)
    target_link_libraries(rimau-http-response-tests PRIVATE rimau_core)

    add_executable(rimau-http1-session-tests
        tests/test_http1_session.cpp)
    target_link_libraries(rimau-http1-session-tests PRIVATE rimau_core)

    add_executable(rimau-http-fuzz-tests
        tests/test_http_fuzz.cpp)
    target_link_libraries(rimau-http-fuzz-tests PRIVATE rimau_core)

    add_executable(rimau-config-database-tests
        tests/test_config_database.cpp)
    target_link_libraries(rimau-config-database-tests PRIVATE rimau_core)

    add_executable(rimau-protocol-capabilities-tests
        tests/test_protocol_capabilities.cpp)
    target_link_libraries(rimau-protocol-capabilities-tests PRIVATE rimau_core)

    add_executable(rimau-http2-wire-tests
        tests/test_http2_wire.cpp)
    target_link_libraries(rimau-http2-wire-tests PRIVATE rimau_core)

    add_executable(rimau-http3-wire-tests
        tests/test_http3_wire.cpp)
    target_link_libraries(rimau-http3-wire-tests PRIVATE rimau_core)

    add_executable(rimau-virtual-host-tests
        tests/test_virtual_host.cpp)
    target_link_libraries(rimau-virtual-host-tests PRIVATE rimau_core)

    add_executable(rimau-waf-tests
        tests/test_waf.cpp)
    target_link_libraries(rimau-waf-tests PRIVATE rimau_core)

    add_test(NAME rimau_http_parser COMMAND rimau-http-parser-tests)
    add_test(NAME rimau_handler_pipeline COMMAND rimau-handler-pipeline-tests)
    add_test(NAME rimau_http_response COMMAND rimau-http-response-tests)
    add_test(NAME rimau_http1_session COMMAND rimau-http1-session-tests)
    add_test(NAME rimau_http_fuzz COMMAND rimau-http-fuzz-tests)
    add_test(NAME rimau_config_database COMMAND rimau-config-database-tests)
    add_test(NAME rimau_protocol_capabilities COMMAND rimau-protocol-capabilities-tests)
    add_test(NAME rimau_http2_wire COMMAND rimau-http2-wire-tests)
    add_test(NAME rimau_http3_wire COMMAND rimau-http3-wire-tests)
    add_test(NAME rimau_virtual_host COMMAND rimau-virtual-host-tests)
    add_test(NAME rimau_waf COMMAND rimau-waf-tests)
    add_test(
        NAME rimau_cli_config
        COMMAND
            ${CMAKE_COMMAND}
            "-DRIMAU_SERVER=$<TARGET_FILE:rimau-server>"
            "-DRIMAU_TEST_DIR=${CMAKE_BINARY_DIR}/test-runtime"
            -P "${CMAKE_SOURCE_DIR}/tests/test_cli_config.cmake")
    add_test(
        NAME rimau_http1_network
        COMMAND
            "${RIMAU_PYTHON3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/test_http1_network.py"
            "$<TARGET_FILE:rimau-server>"
            "${CMAKE_BINARY_DIR}/test-runtime")
    add_test(
        NAME rimau_tls_alpn_h2_curl
        COMMAND
            "${RIMAU_PYTHON3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/test_tls_alpn_h2_curl.py"
            "$<TARGET_FILE:rimau-server>"
            "${CMAKE_BINARY_DIR}/test-runtime"
            "${RIMAU_OPENSSL_INSTALL_DIR}/bin/openssl")
    set_tests_properties(rimau_tls_alpn_h2_curl PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:")
    add_test(
        NAME rimau_tls_sni_cert_selection
        COMMAND
            "${RIMAU_PYTHON3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/test_tls_sni_cert_selection.py"
            "$<TARGET_FILE:rimau-server>"
            "${CMAKE_BINARY_DIR}/test-runtime"
            "${RIMAU_OPENSSL_INSTALL_DIR}/bin/openssl")
    add_test(
        NAME rimau_static_elf_checks
        COMMAND
            "${RIMAU_PYTHON3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/test_static_elf.py"
            "$<TARGET_FILE:rimau-server>"
            "${RIMAU_EXPECT_FULLY_STATIC_TEST}")
    set_tests_properties(rimau_static_elf_checks PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:")
endif()

install(TARGETS rimau-server RUNTIME DESTINATION bin)
install(DIRECTORY public/ DESTINATION share/rimau/public)
