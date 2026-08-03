find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(DIGITOR_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT DIGITOR_GLSLC)
  message(FATAL_ERROR "glslc is required to build Windows Vulkan effect shaders")
endif()

get_filename_component(_digitor_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_shader "${_digitor_repo_root}/shaders/vulkan/digitor_builtin_effect.comp")
set(_embed_tool "${_digitor_repo_root}/tools/embed_spirv.py")
set(_generated "${CMAKE_BINARY_DIR}/generated/digitor")
file(MAKE_DIRECTORY "${_generated}")

set(DIGITOR_WINDOWS_VULKAN_EFFECT_SHADER_HEADERS "")
foreach(_variant IN ITEMS rgba8 rgba16f)
  if(_variant STREQUAL "rgba16f")
    set(_define "DIGITOR_EFFECT_RGBA16F=1")
  else()
    set(_define "DIGITOR_EFFECT_RGBA8=1")
  endif()
  set(_spv "${_generated}/digitor_windows_builtin_effect_${_variant}.spv")
  set(_hpp "${_generated}/digitor_windows_builtin_effect_${_variant}_spv.hpp")
  add_custom_command(
    OUTPUT "${_spv}"
    COMMAND "${DIGITOR_GLSLC}" -fshader-stage=compute -O -D${_define}
            "${_shader}" -o "${_spv}"
    DEPENDS "${_shader}"
    VERBATIM)
  add_custom_command(
    OUTPUT "${_hpp}"
    COMMAND Python3::Interpreter
            "${_embed_tool}"
            "${_spv}" "digitor_windows_builtin_effect_${_variant}_spv" "${_hpp}"
    DEPENDS "${_spv}" "${_embed_tool}"
    VERBATIM)
  list(APPEND DIGITOR_WINDOWS_VULKAN_EFFECT_SHADER_HEADERS "${_hpp}")
endforeach()

add_custom_target(digitor_windows_vulkan_effect_shaders
  DEPENDS ${DIGITOR_WINDOWS_VULKAN_EFFECT_SHADER_HEADERS})

function(digitor_configure_windows_vulkan_effect_shaders target)
  if(NOT WIN32)
    message(FATAL_ERROR "Windows Vulkan effect shaders may only be configured on Windows")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "Windows Vulkan shader target does not exist: ${target}")
  endif()
  find_package(Vulkan REQUIRED)
  target_sources(${target} PRIVATE
    "${_digitor_repo_root}/src/platform/windows/windows_vulkan_builtin_effect_shaders.cpp"
    ${DIGITOR_WINDOWS_VULKAN_EFFECT_SHADER_HEADERS})
  target_include_directories(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated")
  target_link_libraries(${target} PRIVATE Vulkan::Vulkan)
  add_dependencies(${target} digitor_windows_vulkan_effect_shaders)
endfunction()
