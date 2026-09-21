# MSVC's localized /showIncludes prefix must match the bytes Ninja receives.
# CMake's initial compiler probe can decode it with the wrong Windows code page.
# Probe a known header with raw output after compiler/platform initialization.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/include-prefix-probe")
    file(MAKE_DIRECTORY "${probe_dir}")
    set(probe_header "${probe_dir}/hellojson_dependency_probe.h")
    file(WRITE "${probe_header}" "// Dependency prefix probe\n")
    file(WRITE "${probe_dir}/probe.cpp" "#include \"${probe_header}\"\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /utf-8 /showIncludes /c
            "/Fo${probe_dir}/probe.obj" "${probe_dir}/probe.cpp"
        OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_errors
        RESULT_VARIABLE probe_result ENCODING NONE)
    if(NOT probe_result EQUAL 0)
        message(FATAL_ERROR "Cannot verify MSVC header dependency tracking: ${probe_errors}")
    endif()
    string(REPLACE "\\" "/" probe_output "${probe_output}")
    string(REPLACE "\r" "" probe_output "${probe_output}")
    string(REPLACE "\n" ";" probe_lines "${probe_output}")
    set(prefix_found FALSE)
    foreach(line IN LISTS probe_lines)
        string(FIND "${line}" "${probe_header}" header_position)
        if(header_position GREATER 0)
            string(SUBSTRING "${line}" 0 ${header_position} CMAKE_CL_SHOWINCLUDES_PREFIX)
            set(prefix_found TRUE)
            break()
        endif()
    endforeach()
    if(NOT prefix_found)
        message(FATAL_ERROR "Cannot identify MSVC /showIncludes prefix; refusing unsafe incremental builds.")
    endif()
endif()
