find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(DIGITOR_GLSLC glslc)
if(NOT DIGITOR_GLSLC)
  message(FATAL_ERROR "glslc is required to build Android Vulkan effect shaders")
endif()

set(_shader "${CMAKE_SOURCE_DIR}/shaders/vulkan/digitor_builtin_effect.comp")
set(_generated "${CMAKE_BINARY_DIR}/generated/digitor")
file(MAKE_DIRECTORY "${_generated}")

foreach(_variant IN ITEMS rgba8 rgba16f)
  if(_variant STREQUAL "rgba16f")
    set(_define "DIGITOR_EFFECT_RGBA16F=1")
  else()
    set(_define "DIGITOR_EFFECT_RGBA8=1")
  endif()
  set(_spv "${_generated}/digitor_builtin_effect_${_variant}.spv")
  set(_hpp "${_generated}/digitor_builtin_effect_${_variant}_spv.hpp")
  add_custom_command(
    OUTPUT "${_spv}"
    COMMAND "${DIGITOR_GLSLC}" -fshader-stage=compute -O -D${_define}
            "${_shader}" -o "${_spv}"
    DEPENDS "${_shader}"
    VERBATIM)
  add_custom_command(
    OUTPUT "${_hpp}"
    COMMAND Python3::Interpreter
            "${CMAKE_SOURCE_DIR}/tools/embed_spirv.py"
            "${_spv}" "digitor_builtin_effect_${_variant}_spv" "${_hpp}"
    DEPENDS "${_spv}" "${CMAKE_SOURCE_DIR}/tools/embed_spirv.py"
    VERBATIM)
  list(APPEND DIGITOR_ANDROID_VULKAN_EFFECT_SHADER_HEADERS "${_hpp}")
endforeach()

add_custom_target(digitor_android_vulkan_effect_shaders
  DEPENDS ${DIGITOR_ANDROID_VULKAN_EFFECT_SHADER_HEADERS})
