if(NOT DEFINED RIMAU_SERVER)
    message(FATAL_ERROR "RIMAU_SERVER is required")
endif()

if(NOT DEFINED RIMAU_TEST_DIR)
    message(FATAL_ERROR "RIMAU_TEST_DIR is required")
endif()

file(MAKE_DIRECTORY "${RIMAU_TEST_DIR}")
set(database_path "${RIMAU_TEST_DIR}/rimau-cli-config.sqlite3")
file(REMOVE "${database_path}" "${database_path}-shm" "${database_path}-wal")

function(run_rimau expected_result output_variable)
    execute_process(
        COMMAND "${RIMAU_SERVER}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)

    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR
            "Expected exit code ${expected_result}, got ${result}\n"
            "Command: ${RIMAU_SERVER} ${ARGN}\n"
            "stdout:\n${output}\n"
            "stderr:\n${error}")
    endif()

    set(${output_variable} "${output}" PARENT_SCOPE)
    set(${output_variable}_ERROR "${error}" PARENT_SCOPE)
endfunction()

function(require_match variable_name regex label)
    if(NOT "${${variable_name}}" MATCHES "${regex}")
        message(FATAL_ERROR "Missing ${label}; expected regex '${regex}' in output:\n${${variable_name}}")
    endif()
endfunction()

run_rimau(0 default_config --database "${database_path}" --check-config)
require_match(default_config "host=0\\.0\\.0\\.0" "default host")
require_match(default_config "port=8080" "default port")
require_match(default_config "http1_enabled=true" "default HTTP/1.1 flag")

if(NOT EXISTS "${database_path}")
    message(FATAL_ERROR "CLI did not create SQLite database at ${database_path}")
endif()

run_rimau(
    0
    set_config
    --database "${database_path}"
    --set host=127.0.0.1
    --set port=18181
    --set http2_enabled=true
    --set tls_alpn_protocols=h2,http/1.1)
require_match(set_config "host=127\\.0\\.0\\.1" "updated host in --set output")
require_match(set_config "port=18181" "updated port in --set output")
require_match(set_config "http2_enabled=true" "updated HTTP/2 flag in --set output")
require_match(set_config "tls_alpn_protocols=h2,http/1\\.1" "updated ALPN protocols in --set output")

run_rimau(0 updated_config --database "${database_path}" --check-config)
require_match(updated_config "host=127\\.0\\.0\\.1" "updated host in --check-config output")
require_match(updated_config "port=18181" "updated port in --check-config output")
require_match(updated_config "http2_enabled=true" "updated HTTP/2 flag in --check-config output")

run_rimau(0 protocol_output --database "${database_path}" --protocols)
require_match(protocol_output "HTTP/1\\.1: implemented, configured=enabled" "HTTP/1.1 protocol status")
require_match(protocol_output "HTTP/2: partial, configured=enabled" "HTTP/2 protocol status")
require_match(protocol_output "HTTP/3: partial, configured=disabled" "HTTP/3 protocol status")

run_rimau(
    0
    set_protocol_output
    --database "${database_path}"
    --set http2_enabled=false
    --set tls_alpn_protocols=http/1.1
    --protocols)
require_match(set_protocol_output "HTTP/2: partial, configured=disabled" "HTTP/2 protocol status after --set")

run_rimau(1 invalid_set --database "${database_path}" --set port=70000)
require_match(invalid_set_ERROR "port must be between 1 and 65535" "invalid --set error")

file(REMOVE "${database_path}" "${database_path}-shm" "${database_path}-wal")
