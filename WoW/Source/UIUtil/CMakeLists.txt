file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

# Add-ons that ship inside the executable: the Lua runs from memory and the art is served by SFile.
include(${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake)
set(BUILTIN_DATA "${CMAKE_CURRENT_BINARY_DIR}/BuiltinAddOnData.cpp")
embed_files("${BUILTIN_DATA}" g_builtinBlobs
    "lua:WowDecompBodyShape=${CMAKE_SOURCE_DIR}/Addons/WowDecompBodyShape/WowDecompBodyShape.lua"
    "lua:EsteriaInventory=${CMAKE_SOURCE_DIR}/Addons/EsteriaInventory/EsteriaInventory.lua"
    "file:Interface/EsteriaInventory/MenuIcon.tga=${CMAKE_SOURCE_DIR}/Addons/EsteriaInventory/Textures/MenuIcon.tga"
    "file:Interface/EsteriaInventory/SlotBackdrop.tga=${CMAKE_SOURCE_DIR}/Addons/EsteriaInventory/Textures/SlotBackdrop.tga"
)

add_library(UIUtil STATIC
    ${PRIVATE_SOURCES}
    ${BUILTIN_DATA}
)

target_include_directories(UIUtil
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(UIUtil
    PRIVATE
        zlib
)
