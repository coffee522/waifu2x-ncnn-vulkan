if(NOT DEFINED WAIFU2X_EXECUTABLE OR NOT EXISTS "${WAIFU2X_EXECUTABLE}")
    message(FATAL_ERROR "WAIFU2X_EXECUTABLE must point to a built executable")
endif()
if(NOT DEFINED SOURCE_DIR OR NOT EXISTS "${SOURCE_DIR}/images/0.jpg")
    message(FATAL_ERROR "SOURCE_DIR must point to the repository root")
endif()
if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "OUTPUT_DIR is required")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(INPUT_PATH "${SOURCE_DIR}/images/0.jpg")
set(MODEL_PATH "${SOURCE_DIR}/models/models-cunet")
set(OUTPUT_PATH "${OUTPUT_DIR}/smoke.webp")
set(UNICODE_INPUT_DIR "${OUTPUT_DIR}/中文輸入")
set(UNICODE_INPUT_PATH "${UNICODE_INPUT_DIR}/漫畫 頁面.jpg")
set(UNICODE_OUTPUT_DIR "${OUTPUT_DIR}/中文輸出")
set(UNICODE_OUTPUT_PATH "${UNICODE_OUTPUT_DIR}/升頻 頁面.webp")
set(LIST_PATH "${OUTPUT_DIR}/smoke.tsv")
set(INVALID_OUTPUT_PATH "${OUTPUT_DIR}/invalid.webp")
set(INVALID_LIST_PATH "${OUTPUT_DIR}/invalid.tsv")
set(CORRUPT_INPUT_PATH "${OUTPUT_DIR}/corrupt.png")
set(CORRUPT_OUTPUT_PATH "${OUTPUT_DIR}/corrupt.webp")
set(CORRUPT_LIST_PATH "${OUTPUT_DIR}/corrupt.tsv")
set(WEBP_INPUT_OUTPUT_PATH "${OUTPUT_DIR}/webp-input.webp")
set(WEBP_INPUT_LIST_PATH "${OUTPUT_DIR}/webp-input.tsv")
set(PNG_OUTPUT_PATH "${OUTPUT_DIR}/smoke.png")
set(PNG_LIST_PATH "${OUTPUT_DIR}/png.tsv")
set(MISMATCH_OUTPUT_PATH "${OUTPUT_DIR}/mismatch.webp")

file(REMOVE
    "${OUTPUT_PATH}"
    "${OUTPUT_PATH}.tmp"
    "${LIST_PATH}"
    "${INVALID_OUTPUT_PATH}"
    "${INVALID_OUTPUT_PATH}.tmp"
    "${INVALID_LIST_PATH}"
    "${CORRUPT_INPUT_PATH}"
    "${CORRUPT_OUTPUT_PATH}"
    "${CORRUPT_OUTPUT_PATH}.tmp"
    "${CORRUPT_LIST_PATH}"
    "${WEBP_INPUT_OUTPUT_PATH}"
    "${WEBP_INPUT_OUTPUT_PATH}.tmp"
    "${WEBP_INPUT_LIST_PATH}"
    "${PNG_OUTPUT_PATH}"
    "${PNG_OUTPUT_PATH}.tmp"
    "${PNG_LIST_PATH}"
    "${MISMATCH_OUTPUT_PATH}"
    "${MISMATCH_OUTPUT_PATH}.tmp"
)
file(REMOVE_RECURSE "${UNICODE_INPUT_DIR}" "${UNICODE_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${UNICODE_INPUT_DIR}" "${UNICODE_OUTPUT_DIR}")
configure_file("${INPUT_PATH}" "${UNICODE_INPUT_PATH}" COPYONLY)

file(WRITE "${LIST_PATH}"
    "${INPUT_PATH}\t${OUTPUT_PATH}\n"
    "${UNICODE_INPUT_PATH}\t${UNICODE_OUTPUT_PATH}\n"
)
execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -l "${LIST_PATH}"
        -n 0
        -s 1
        -g -1
        -j "1:1:2"
        -m "${MODEL_PATH}"
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR
)
if(NOT RESULT STREQUAL "0")
    message(FATAL_ERROR "valid smoke job failed (${RESULT})\nstdout:\n${STDOUT}\nstderr:\n${STDERR}")
endif()
function(assert_webp OUTPUT_FILE)
    if(NOT EXISTS "${OUTPUT_FILE}")
        message(FATAL_ERROR "valid smoke job did not create ${OUTPUT_FILE}")
    endif()
    if(EXISTS "${OUTPUT_FILE}.tmp")
        message(FATAL_ERROR "valid smoke job left temporary output ${OUTPUT_FILE}.tmp")
    endif()

    file(READ "${OUTPUT_FILE}" SIGNATURE OFFSET 0 LIMIT 12 HEX)
    string(TOUPPER "${SIGNATURE}" SIGNATURE)
    string(SUBSTRING "${SIGNATURE}" 0 8 RIFF_SIGNATURE)
    string(SUBSTRING "${SIGNATURE}" 16 8 WEBP_SIGNATURE)
    if(NOT RIFF_SIGNATURE STREQUAL "52494646" OR NOT WEBP_SIGNATURE STREQUAL "57454250")
        message(FATAL_ERROR "${OUTPUT_FILE} is not a RIFF WebP file")
    endif()
endfunction()

assert_webp("${OUTPUT_PATH}")
assert_webp("${UNICODE_OUTPUT_PATH}")

file(WRITE "${WEBP_INPUT_LIST_PATH}" "${OUTPUT_PATH}\t${WEBP_INPUT_OUTPUT_PATH}\n")
execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -l "${WEBP_INPUT_LIST_PATH}"
        -n -1
        -s 1
        -g -1
        -j "1:1:2"
        -m "${MODEL_PATH}"
    RESULT_VARIABLE WEBP_INPUT_RESULT
    OUTPUT_VARIABLE WEBP_INPUT_STDOUT
    ERROR_VARIABLE WEBP_INPUT_STDERR
)
if(NOT WEBP_INPUT_RESULT STREQUAL "0")
    message(FATAL_ERROR "WebP input job failed (${WEBP_INPUT_RESULT})\nstdout:\n${WEBP_INPUT_STDOUT}\nstderr:\n${WEBP_INPUT_STDERR}")
endif()
assert_webp("${WEBP_INPUT_OUTPUT_PATH}")

function(assert_png OUTPUT_FILE)
    if(NOT EXISTS "${OUTPUT_FILE}")
        message(FATAL_ERROR "PNG smoke job did not create ${OUTPUT_FILE}")
    endif()
    if(EXISTS "${OUTPUT_FILE}.tmp")
        message(FATAL_ERROR "PNG smoke job left temporary output ${OUTPUT_FILE}.tmp")
    endif()
    file(READ "${OUTPUT_FILE}" SIGNATURE OFFSET 0 LIMIT 8 HEX)
    string(TOUPPER "${SIGNATURE}" SIGNATURE)
    if(NOT SIGNATURE STREQUAL "89504E470D0A1A0A")
        message(FATAL_ERROR "${OUTPUT_FILE} is not a PNG file")
    endif()
endfunction()

file(WRITE "${PNG_LIST_PATH}" "${INPUT_PATH}\t${PNG_OUTPUT_PATH}\n")
execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -l "${PNG_LIST_PATH}"
        -f png
        -n 0
        -s 1
        -g -1
        -j "1:1:1"
        -m "${MODEL_PATH}"
    RESULT_VARIABLE PNG_RESULT
    OUTPUT_VARIABLE PNG_STDOUT
    ERROR_VARIABLE PNG_STDERR
)
if(NOT PNG_RESULT STREQUAL "0")
    message(FATAL_ERROR "PNG smoke job failed (${PNG_RESULT})\nstdout:\n${PNG_STDOUT}\nstderr:\n${PNG_STDERR}")
endif()
assert_png("${PNG_OUTPUT_PATH}")

execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -i "${INPUT_PATH}"
        -o "${MISMATCH_OUTPUT_PATH}"
        -f png
        -g -1
        -m "${MODEL_PATH}"
    RESULT_VARIABLE MISMATCH_RESULT
)
if(MISMATCH_RESULT STREQUAL "0")
    message(FATAL_ERROR "format/extension mismatch unexpectedly succeeded")
endif()
if(EXISTS "${MISMATCH_OUTPUT_PATH}" OR EXISTS "${MISMATCH_OUTPUT_PATH}.tmp")
    message(FATAL_ERROR "format/extension mismatch created output files")
endif()

execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -l "${PNG_LIST_PATH}"
        -f png
        -q 85
        -g -1
        -m "${MODEL_PATH}"
    RESULT_VARIABLE PNG_WEBP_OPTION_RESULT
)
if(PNG_WEBP_OPTION_RESULT STREQUAL "0")
    message(FATAL_ERROR "PNG accepted WebP-only settings")
endif()

execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -l "${LIST_PATH}"
        -f webp
        -q 101
        -g -1
        -m "${MODEL_PATH}"
    RESULT_VARIABLE QUALITY_RESULT
)
if(QUALITY_RESULT STREQUAL "0")
    message(FATAL_ERROR "out-of-range WebP quality unexpectedly succeeded")
endif()

execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -l "${LIST_PATH}"
        -f webp
        -c 7
        -g -1
        -m "${MODEL_PATH}"
    RESULT_VARIABLE METHOD_RESULT
)
if(METHOD_RESULT STREQUAL "0")
    message(FATAL_ERROR "out-of-range WebP method unexpectedly succeeded")
endif()

execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}" -l "${LIST_PATH}" -g -1 -m "${MODEL_PATH}"
    RESULT_VARIABLE OVERWRITE_RESULT
    OUTPUT_VARIABLE OVERWRITE_STDOUT
    ERROR_VARIABLE OVERWRITE_STDERR
)
if(OVERWRITE_RESULT STREQUAL "0")
    message(FATAL_ERROR "existing outputs were unexpectedly overwritten")
endif()
if(EXISTS "${OUTPUT_PATH}.tmp" OR EXISTS "${UNICODE_OUTPUT_PATH}.tmp")
    message(FATAL_ERROR "overwrite rejection left temporary outputs")
endif()

file(WRITE "${INVALID_LIST_PATH}" "${SOURCE_DIR}/images/missing.png\t${INVALID_OUTPUT_PATH}\n")
execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}" -l "${INVALID_LIST_PATH}" -g -1 -m "${MODEL_PATH}"
    RESULT_VARIABLE INVALID_RESULT
    OUTPUT_VARIABLE INVALID_STDOUT
    ERROR_VARIABLE INVALID_STDERR
)
if(INVALID_RESULT STREQUAL "0")
    message(FATAL_ERROR "invalid smoke job unexpectedly succeeded")
endif()
if(EXISTS "${INVALID_OUTPUT_PATH}" OR EXISTS "${INVALID_OUTPUT_PATH}.tmp")
    message(FATAL_ERROR "invalid smoke job created output files")
endif()

file(WRITE "${CORRUPT_INPUT_PATH}" "this is not a PNG image")
file(WRITE "${CORRUPT_LIST_PATH}" "${CORRUPT_INPUT_PATH}\t${CORRUPT_OUTPUT_PATH}\n")
execute_process(
    COMMAND "${WAIFU2X_EXECUTABLE}"
        -l "${CORRUPT_LIST_PATH}"
        -n 0
        -s 1
        -g -1
        -j "1:1:2"
        -m "${MODEL_PATH}"
    RESULT_VARIABLE CORRUPT_RESULT
    OUTPUT_VARIABLE CORRUPT_STDOUT
    ERROR_VARIABLE CORRUPT_STDERR
)
if(CORRUPT_RESULT STREQUAL "0")
    message(FATAL_ERROR "corrupt image job unexpectedly succeeded")
endif()
if(EXISTS "${CORRUPT_OUTPUT_PATH}" OR EXISTS "${CORRUPT_OUTPUT_PATH}.tmp")
    message(FATAL_ERROR "corrupt image job created output files")
endif()

message(STATUS "waifu2x smoke test passed\n${STDOUT}")
