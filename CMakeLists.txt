#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

GOOD_COMMIT = "ad14307a5ef465d8028b069ec6eb5db857eae263"
cmake_path = Path("CMakeLists.txt")

def run(*args: str) -> str:
    return subprocess.check_output(args, text=True)

try:
    original = run("git", "show", f"{GOOD_COMMIT}:CMakeLists.txt")
except Exception as exc:
    print(f"Failed to restore CMakeLists.txt from {GOOD_COMMIT}: {exc}", file=sys.stderr)
    sys.exit(1)

anchor = '''        set_tests_properties(digitor_native_gpu_tests PROPERTIES
            ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
            LABELS "native-gpu;hardware"
            SKIP_RETURN_CODE 77
        )
'''

block = '''
    # v4.9.0 feature-specific production qualification.
    add_executable(digitor_log_wheels_native_tests
        tests/test_log_wheels_native.cpp
    )
    set_target_properties(digitor_log_wheels_native_tests PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(digitor_log_wheels_native_tests PRIVATE Digitor::Engine)
    target_include_directories(digitor_log_wheels_native_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if (MSVC)
        target_compile_options(digitor_log_wheels_native_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(digitor_log_wheels_native_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME digitor_log_wheels_native_tests
        COMMAND digitor_log_wheels_native_tests
    )
    set_tests_properties(digitor_log_wheels_native_tests PROPERTIES
        ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
        LABELS "native-gpu;hardware;log-wheels;v4.9.0"
        SKIP_RETURN_CODE 77
    )
'''

if "add_executable(digitor_log_wheels_native_tests" not in original:
    if anchor not in original:
        print("Could not find native GPU test anchor in restored CMakeLists.txt.", file=sys.stderr)
        sys.exit(1)
    original = original.replace(anchor, anchor + block, 1)

cmake_path.write_text(original, encoding="utf-8", newline="\n")
print("Restored full CMakeLists.txt and added digitor_log_wheels_native_tests.")
