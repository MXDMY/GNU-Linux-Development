# cmake最低版本号要求
cmake_minimum_required(VERSION 3.5)
# 项目名称
project(modules LANGUAGES C)
# 选择要使用的编译器，请确保系统环境变量包含编译器路径
set(CMAKE_C_COMPILER "gcc")
# 设置编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Og -Wall -Wextra")

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# cJSON 1.7.19：头文件与实现均在库根目录
set(CJSON_DIR "${THIRD_PARTY_DIR}/cJSON-1.7.19")
set(CJSON_SRCS
    "${CJSON_DIR}/cJSON.c"
    "${CJSON_DIR}/cJSON_Utils.c"
)
set(CJSON_INCS
    "${CJSON_DIR}"
)

# Mbed TLS 4.2.0：不进入 everest / p256-m / pqcp
set(MBEDTLS_DIR "${THIRD_PARTY_DIR}/mbedtls-4.2.0")
file(GLOB MBEDTLS_SRCS
    "${MBEDTLS_DIR}/library/*.c"
    "${MBEDTLS_DIR}/tf-psa-crypto/core/*.c"
    "${MBEDTLS_DIR}/tf-psa-crypto/platform/*.c"
    "${MBEDTLS_DIR}/tf-psa-crypto/utilities/*.c"
    "${MBEDTLS_DIR}/tf-psa-crypto/extras/*.c"
    "${MBEDTLS_DIR}/tf-psa-crypto/drivers/builtin/src/*.c"
)
set(MBEDTLS_INCS
    "${MBEDTLS_DIR}/include"
    "${MBEDTLS_DIR}/tf-psa-crypto/include"
    "${MBEDTLS_DIR}/tf-psa-crypto/drivers/builtin/include"
    "${MBEDTLS_DIR}/library"
    "${MBEDTLS_DIR}/tf-psa-crypto/core"
    "${MBEDTLS_DIR}/tf-psa-crypto/dispatch"
    "${MBEDTLS_DIR}/tf-psa-crypto/platform"
    "${MBEDTLS_DIR}/tf-psa-crypto/utilities"
    "${MBEDTLS_DIR}/tf-psa-crypto/extras"
    "${MBEDTLS_DIR}/tf-psa-crypto/drivers/builtin/src"
)

# 只收集 modules/ 与 modules/third_party/ 下的 *_test.c，不进入开源库目录
file(GLOB MODULE_TEST_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/*_test.c"
    "${THIRD_PARTY_DIR}/*_test.c"
)

foreach(TEST_SRC ${MODULE_TEST_SRCS})
    get_filename_component(TEST_NAME "${TEST_SRC}" NAME_WE)

    if(TEST_NAME STREQUAL "cJSON_test")
        add_executable(${TEST_NAME} ${TEST_SRC} ${CJSON_SRCS})
        target_include_directories(${TEST_NAME} PRIVATE ${CJSON_INCS})
        target_compile_options(${TEST_NAME} PRIVATE -std=c99)
        target_link_libraries(${TEST_NAME} m)
    elseif(TEST_NAME STREQUAL "mbedtls_test")
        add_executable(${TEST_NAME} ${TEST_SRC} ${MBEDTLS_SRCS})
        target_include_directories(${TEST_NAME} PRIVATE ${MBEDTLS_INCS})
        target_compile_options(${TEST_NAME} PRIVATE -std=gnu11)
    else()
        message(FATAL_ERROR "No library mapping for ${TEST_SRC}")
    endif()
endforeach()
