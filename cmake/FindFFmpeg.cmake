# Find the FFmpeg libraries used by DigitorEngine.  Supports pkg-config and
# deterministic lookup below DIGITOR_FFMPEG_ROOT (or its environment variable).
include(FindPackageHandleStandardArgs)
set(DIGITOR_FFMPEG_ROOT "${DIGITOR_FFMPEG_ROOT}" CACHE PATH "FFmpeg SDK prefix")
if(NOT DIGITOR_FFMPEG_ROOT AND DEFINED ENV{DIGITOR_FFMPEG_ROOT})
  set(DIGITOR_FFMPEG_ROOT "$ENV{DIGITOR_FFMPEG_ROOT}")
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_FFMPEG QUIET libavcodec libavformat libavutil libswscale libswresample)
endif()

set(_ffmpeg_hints)
if(DIGITOR_FFMPEG_ROOT)
  list(APPEND _ffmpeg_hints "${DIGITOR_FFMPEG_ROOT}")
endif()
find_path(FFmpeg_INCLUDE_DIR libavcodec/avcodec.h HINTS ${_ffmpeg_hints} ${PC_FFMPEG_INCLUDE_DIRS} PATH_SUFFIXES include)
set(_ffmpeg_libraries)
set(_ffmpeg_required FFmpeg_INCLUDE_DIR)
foreach(_component IN ITEMS avcodec avformat avutil swscale swresample)
  string(TOUPPER "${_component}" _upper)
  find_library(FFmpeg_${_upper}_LIBRARY NAMES ${_component} lib${_component}
    HINTS ${_ffmpeg_hints} ${PC_FFMPEG_LIBRARY_DIRS} PATH_SUFFIXES lib lib64 bin)
  list(APPEND _ffmpeg_libraries "${FFmpeg_${_upper}_LIBRARY}")
  list(APPEND _ffmpeg_required "FFmpeg_${_upper}_LIBRARY")
endforeach()
set(FFmpeg_LIBRARIES "${_ffmpeg_libraries}")
# Public result for callers that need the concrete development-library set.
# This deliberately contains libraries only; the optional CLI is discovered
# separately as DIGITOR_FFMPEG_CLI by the top-level project.
set(DIGITOR_FFMPEG_LIBRARIES "${_ffmpeg_libraries}" CACHE STRING
  "FFmpeg development libraries (avcodec, avformat, avutil, swscale, swresample)" FORCE)
set(FFmpeg_VERSION "${PC_FFMPEG_VERSION}")
if(NOT FFmpeg_VERSION)
  set(FFmpeg_VERSION "unknown")
endif()
find_package_handle_standard_args(FFmpeg REQUIRED_VARS ${_ffmpeg_required} VERSION_VAR FFmpeg_VERSION)
if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
  add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
  set_target_properties(FFmpeg::FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${_ffmpeg_libraries}")
endif()
mark_as_advanced(FFmpeg_INCLUDE_DIR FFmpeg_AVCODEC_LIBRARY FFmpeg_AVFORMAT_LIBRARY FFmpeg_AVUTIL_LIBRARY FFmpeg_SWSCALE_LIBRARY FFmpeg_SWRESAMPLE_LIBRARY)
