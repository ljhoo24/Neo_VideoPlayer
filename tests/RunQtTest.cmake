foreach(_required
        VP_TEST_EXECUTABLE
        VP_TEST_OUTPUT
        VP_QT_PLUGIN_PATH
        VP_QT_RUNTIME_PATH)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required test variable: ${_required}")
    endif()
endforeach()

# QtTest on Windows may route its default text output to the debugger when
# CTest owns the process pipes. Write a deterministic report, then replay it
# through CMake so --output-on-failure always has useful diagnostics.
file(REMOVE "${VP_TEST_OUTPUT}")
set(ENV{QT_QPA_PLATFORM} "offscreen")
set(ENV{QT_PLUGIN_PATH} "${VP_QT_PLUGIN_PATH}")
set(ENV{PATH} "${VP_QT_RUNTIME_PATH}")

execute_process(
    COMMAND "${VP_TEST_EXECUTABLE}" -o "${VP_TEST_OUTPUT},txt"
    RESULT_VARIABLE _result
    ERROR_VARIABLE _stderr
)

set(_report "")
if(EXISTS "${VP_TEST_OUTPUT}")
    file(READ "${VP_TEST_OUTPUT}" _report)
    file(REMOVE "${VP_TEST_OUTPUT}")
endif()

if(NOT "${_stderr}" STREQUAL "")
    string(APPEND _report "\n${_stderr}")
endif()
message("${_report}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "QtTest failed with exit code ${_result}")
endif()
