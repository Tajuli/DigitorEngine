
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was DigitorEngineConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)
find_dependency(Threads)
if("OFF" STREQUAL "ON")
    find_dependency(OpenColorIO 2 CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/DigitorEngineTargets.cmake")

# Support both single-config NOCONFIG exports and multi-config Release exports.
# CMake consumers may configure without CMAKE_BUILD_TYPE, so map only to a
# configuration that the installed target actually provides.
if(TARGET Digitor::Engine)
    get_target_property(_digitor_imported_configs
                        Digitor::Engine IMPORTED_CONFIGURATIONS)
    if(_digitor_imported_configs)
        if("NOCONFIG" IN_LIST _digitor_imported_configs)
            set_property(TARGET Digitor::Engine PROPERTY MAP_IMPORTED_CONFIG_RELEASE NOCONFIG)
            set_property(TARGET Digitor::Engine PROPERTY MAP_IMPORTED_CONFIG_RELWITHDEBINFO NOCONFIG)
            set_property(TARGET Digitor::Engine PROPERTY MAP_IMPORTED_CONFIG_MINSIZEREL NOCONFIG)
            set_property(TARGET Digitor::Engine PROPERTY MAP_IMPORTED_CONFIG_DEBUG NOCONFIG)
        elseif("RELEASE" IN_LIST _digitor_imported_configs)
            set_property(TARGET Digitor::Engine PROPERTY MAP_IMPORTED_CONFIG_NOCONFIG Release)
        endif()
    endif()
endif()

check_required_components(DigitorEngine)
