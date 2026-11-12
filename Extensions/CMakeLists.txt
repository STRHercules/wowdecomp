file(GLOB EXTENSION_CANDIDATES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")

set(EXTENSION_NAMES "")
set(STANDALONE_EXTENSIONS "")
foreach (candidate ${EXTENSION_CANDIDATES})
    # A self-contained module (own CMakeLists.txt, host hooks) is built as a subdirectory, not through the loader.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${candidate}/CMakeLists.txt")
        list(APPEND STANDALONE_EXTENSIONS ${candidate})
    elseif (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${candidate}/src")
        list(APPEND EXTENSION_NAMES ${candidate})
    endif ()
endforeach ()
list(SORT EXTENSION_NAMES)

set(EXTENSION_SOURCES "")
set(EXTENSION_FORWARD_DECL "")
set(EXTENSION_ENTRIES "")
set(EXTENSION_COUNT 0)

# Eclipse lives in its own checkout; opt in when that source is available.
option(WOWDECOMP_EXTENSION_ECLIPSE "Compile the native Eclipse x64/Vulkan extension" OFF)

message(STATUS "Extensions:")

foreach (name ${EXTENSION_NAMES})
    string(TOUPPER ${name} upper)
    option(WOWDECOMP_EXTENSION_${upper} "Compile the ${name} extension into the client" ON)

    if (NOT WOWDECOMP_EXTENSION_${upper})
        message(STATUS "  - ${name} (disabled)")
        continue()
    endif ()

    string(MAKE_C_IDENTIFIER ${name} ident)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/src/*.cpp")
    list(APPEND EXTENSION_SOURCES ${sources})

    string(APPEND EXTENSION_FORWARD_DECL "void Add${ident}Extension();\n")
    string(APPEND EXTENSION_ENTRIES "    { \"${name}\", Add${ident}Extension },\n")
    math(EXPR EXTENSION_COUNT "${EXTENSION_COUNT} + 1")

    # An extension may ship <Name>/<Name>.cmake for its own build needs (defines, third-party code).
    include("${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.cmake" OPTIONAL)

    message(STATUS "  + ${name}")
endforeach ()

# ASECM3 is a self-contained module: its core/client libraries come from its own CMakeLists, the host adapter
# (host/ASECM3Extension.cpp) is the extension entry. ASECM3Host is what Model2/Object link for the direct hooks;
# with the module off it resolves to the no-op Disabled.cpp.
option(WOWDECOMP_ENABLE_ASEC_M3 "Build the ASECM3 native M3 module" ON)
add_library(ASECM3Host INTERFACE)
if (WOWDECOMP_ENABLE_ASEC_M3 AND "ASECM3" IN_LIST STANDALONE_EXTENSIONS)
    add_subdirectory(ASECM3)
    target_link_libraries(ASECM3Host INTERFACE ASECM3Client)
    list(APPEND EXTENSION_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ASECM3/host/ASECM3Extension.cpp)
    string(APPEND EXTENSION_FORWARD_DECL "void AddASECM3Extension();
")
    string(APPEND EXTENSION_ENTRIES "    { \"ASECM3\", AddASECM3Extension },
")
    math(EXPR EXTENSION_COUNT "${EXTENSION_COUNT} + 1")
    message(STATUS "  + ASECM3")
else ()
    add_library(ASECM3Disabled STATIC ASECM3/client/Disabled.cpp)
    target_include_directories(ASECM3Disabled PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ASECM3/client)
    target_link_libraries(ASECM3Host INTERFACE ASECM3Disabled)
    message(STATUS "  - ASECM3 (disabled)")
endif ()

set(EXTENSION_LOADER ${CMAKE_CURRENT_BINARY_DIR}/ExtensionsLoader.cpp)
configure_file(ExtensionsLoader.cpp.in ${EXTENSION_LOADER} @ONLY)

add_library(WowExtensions STATIC
    ${EXTENSION_LOADER}
    ${EXTENSION_SOURCES}
)

target_include_directories(WowExtensions
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(WowExtensions
    PUBLIC
        Extension
    PRIVATE
        ASECM3Host
)

if (WOWDECOMP_RETAIL_ASSETS)
    target_link_libraries(WowExtensions PRIVATE RetailAssets)
endif ()

if (TARGET EclipseNative)
    target_link_libraries(WowExtensions PRIVATE EclipseNative)
endif ()
