cmake_minimum_required(VERSION 3.16)
project(Prototype1 CXX)
set(CMAKE_CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)

# ── OpenFHE ────────────────────────────────────────────────────────────────
find_package(OpenFHE CONFIG REQUIRED)
if(OpenFHE_FOUND)
    message(STATUS "OpenFHE ${BASE_OPENFHE_VERSION} found")
else()
    message(FATAL_ERROR "OpenFHE not found")
endif()

set(CMAKE_CXX_FLAGS "${OpenFHE_CXX_FLAGS}")

include_directories(${OpenFHE_INCLUDE})
include_directories(${OpenFHE_INCLUDE}/third-party/include)
include_directories(${OpenFHE_INCLUDE}/core)
include_directories(${OpenFHE_INCLUDE}/pke)
include_directories(${OpenFHE_INCLUDE}/binfhe)
include_directories(${OPENMP_INCLUDES})

if(UNIX AND NOT APPLE)
    add_link_options(-Wl,--no-as-needed)
endif()

link_directories(${OpenFHE_LIBDIR})
link_directories(${OPENMP_LIBRARIES})
set(CMAKE_EXE_LINKER_FLAGS "${OpenFHE_EXE_LINKER_FLAGS}")

# ── Project includes ───────────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Library: proto1 ────────────────────────────────────────────────────────
add_library(proto1 STATIC
    src/crypto/bfv_context.cpp
    src/storage/filesystem_storage.cpp
    src/execution/encrypted_batch.cpp
    src/execution/operators.cpp
    src/expression/expression_executor.cpp
    src/result/result_stream.cpp
    src/client/client.cpp
    src/catalog/ingestion.cpp
    src/planner/plan_validator.cpp
)
target_link_libraries(proto1 PUBLIC ${OpenFHE_SHARED_LIBRARIES})

# ── Demo executable ────────────────────────────────────────────────────────
add_executable(prototype1_demo examples/demo.cpp)
target_link_libraries(prototype1_demo PRIVATE proto1)

# ── GoogleTest ─────────────────────────────────────────────────────────────
set(GTEST_DIR
    ${CMAKE_SOURCE_DIR}/../openfhe-development/third-party/google-test)

if(EXISTS ${GTEST_DIR}/CMakeLists.txt)
    add_subdirectory(${GTEST_DIR} ${CMAKE_BINARY_DIR}/gtest EXCLUDE_FROM_ALL)
    set(GTEST_AVAILABLE TRUE)
    message(STATUS "GoogleTest found at ${GTEST_DIR}")
else()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        set(GTEST_AVAILABLE TRUE)
        message(STATUS "GoogleTest found via find_package")
    else()
        message(WARNING "GoogleTest not found — tests will not be built")
        set(GTEST_AVAILABLE FALSE)
    endif()
endif()

if(GTEST_AVAILABLE)
    enable_testing()

    add_executable(proto1_tests
        tests/test_crypto.cpp
        tests/test_expressions.cpp
        tests/test_execution.cpp
    )

    if(TARGET gtest_main)
        target_link_libraries(proto1_tests PRIVATE proto1 gtest_main)
    else()
        target_link_libraries(proto1_tests PRIVATE proto1 GTest::gtest_main)
    endif()

    include(GoogleTest)
    gtest_discover_tests(proto1_tests)
endif()

# ── Output directories ─────────────────────────────────────────────────────
set_target_properties(prototype1_demo
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(GTEST_AVAILABLE)
    set_target_properties(proto1_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# ── Bitmask test executables ───────────────────────────────────────────────
add_executable(test_bitmask_decrypt examples/test_bitmask_decrypt.cpp)
target_link_libraries(test_bitmask_decrypt PRIVATE proto1)
set_target_properties(test_bitmask_decrypt
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(test_row_filtering examples/test_row_filtering.cpp)
target_link_libraries(test_row_filtering PRIVATE proto1)
set_target_properties(test_row_filtering
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(test_bitmask_filter_exec examples/test_bitmask_filter_exec.cpp)
target_link_libraries(test_bitmask_filter_exec PRIVATE proto1)
set_target_properties(test_bitmask_filter_exec
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
