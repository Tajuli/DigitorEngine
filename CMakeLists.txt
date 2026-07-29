*** Begin Patch
*** Update File: CMakeLists.txt
@@
     add_executable(digitor_native_gpu_tests tests/test_native_gpu.cpp)
         set_target_properties(digitor_native_gpu_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
         target_link_libraries(digitor_native_gpu_tests PRIVATE Digitor::Engine)
         target_include_directories(digitor_native_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
@@
         set_tests_properties(digitor_native_gpu_tests PROPERTIES
             ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
             LABELS "native-gpu;hardware"
             SKIP_RETURN_CODE 77
         )
+
+    # v4.9.0 feature-specific production qualification. This target verifies
+    # real Log Wheels native execution rather than inheriting evidence from
+    # Primary Wheels or RGB Curves.
+    add_executable(digitor_log_wheels_native_tests
+        tests/test_log_wheels_native.cpp
+    )
+    set_target_properties(digitor_log_wheels_native_tests PROPERTIES
+        CXX_STANDARD 20
+        CXX_STANDARD_REQUIRED ON
+        CXX_EXTENSIONS OFF
+    )
+    target_link_libraries(digitor_log_wheels_native_tests PRIVATE Digitor::Engine)
+    target_include_directories(digitor_log_wheels_native_tests PRIVATE
+        ${CMAKE_CURRENT_SOURCE_DIR}/src
+    )
+    if (MSVC)
+        target_compile_options(digitor_log_wheels_native_tests PRIVATE /UNDEBUG)
+    else()
+        target_compile_options(digitor_log_wheels_native_tests PRIVATE -UNDEBUG)
+    endif()
+    add_test(NAME digitor_log_wheels_native_tests
+        COMMAND digitor_log_wheels_native_tests
+    )
+    set_tests_properties(digitor_log_wheels_native_tests PROPERTIES
+        ENVIRONMENT "DIGITOR_GPU_VALIDATION=1"
+        LABELS "native-gpu;hardware;log-wheels;v4.9.0"
+        SKIP_RETURN_CODE 77
+    )
*** End Patch
