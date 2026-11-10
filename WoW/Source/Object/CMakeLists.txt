file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(Object STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Object
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Object
    PRIVATE
        Extension
        ASECM3Host
)

# Unit_C's retail blocks (rider poses, Mount Studio replacements, Ethereal hands) need the define RetailAssets carries.
if (WOWDECOMP_RETAIL_ASSETS)
    target_link_libraries(Object PRIVATE RetailAssets)
endif ()
