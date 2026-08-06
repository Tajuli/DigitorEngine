# Deferred platform dependency wiring for the native image runtime.
# Common C++ and Windows/Android codec sources are aggregated by the existing
# src/media/native_still_image_host.cpp translation unit to preserve package
# target compatibility. Apple ImageIO remains Objective-C++ and is compiled
# separately here.
if(NOT TARGET digitor_engine)
    message(FATAL_ERROR "Digitor native image runtime requires the digitor_engine target")
endif()

if(WIN32)
    target_link_libraries(digitor_engine PRIVATE windowscodecs propsys)
elseif(APPLE)
    target_sources(digitor_engine PRIVATE src/media/apple_imageio_codec.mm)
    set_source_files_properties(src/media/apple_imageio_codec.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    find_library(DIGITOR_IMAGEIO_FRAMEWORK ImageIO REQUIRED)
    find_library(DIGITOR_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(DIGITOR_UTTYPE_FRAMEWORK UniformTypeIdentifiers REQUIRED)
    target_link_libraries(digitor_engine PRIVATE
        ${DIGITOR_IMAGEIO_FRAMEWORK}
        ${DIGITOR_COREGRAPHICS_FRAMEWORK}
        ${DIGITOR_UTTYPE_FRAMEWORK})
elseif(ANDROID)
    find_library(DIGITOR_JNIGRAPHICS_LIBRARY jnigraphics REQUIRED)
    target_link_libraries(digitor_engine PRIVATE ${DIGITOR_JNIGRAPHICS_LIBRARY})
endif()
