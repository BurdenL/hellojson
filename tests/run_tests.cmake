# File logging also works when the Windows Qt test process has no stdout handle.
execute_process(COMMAND "${TEST_EXECUTABLE}" -o "${TEST_LOG},txt"
    RESULT_VARIABLE result TIMEOUT 45)
if(EXISTS "${TEST_LOG}")
    file(READ "${TEST_LOG}" test_output)
    message("${test_output}")
endif()
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "hellojson_tests exited with ${result}")
endif()
