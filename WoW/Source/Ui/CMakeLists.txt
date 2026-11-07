file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (NOT WIN32)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/UILCD/Windows/")
endif ()

add_library(Ui STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Ui
    PRIVATE
        Extension
)

# MinimapFrame finds retail terrain maps' minimap tiles through the retail source
if (WOWDECOMP_RETAIL_ASSETS)
    target_link_libraries(Ui PRIVATE RetailAssets)
endif ()

if (WIN32)
    target_link_libraries(Ui
        PRIVATE
            lgLcd
    )
endif ()
