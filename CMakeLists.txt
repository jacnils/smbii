cmake_minimum_required(VERSION 3.28)
project(smbii VERSION 1.0.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(netkit QUIET)
if (NOT netkit_FOUND)
    FetchContent_Declare(netkit GIT_REPOSITORY https://github.com/jacnils/netkit.git
        GIT_TAG origin/master
    )
    FetchContent_MakeAvailable(netkit)
endif()

find_package(grrlib QUIET)
if (NOT grrlib_FOUND)
    FetchContent_Declare(
        GRRLIB
        GIT_REPOSITORY https://github.com/GRRLIB/GRRLIB.git
        GIT_TAG origin/master
    )
    FetchContent_MakeAvailable(GRRLIB)
endif()

add_executable(smbii
        src/main.cpp
        src/oggplayer.c
        src/libpatcher.c
)

target_include_directories(smbii PRIVATE
        "${DEVKITPRO}/portlibs/ppc/include"
)

target_compile_definitions(smbii PRIVATE SMBII_VERSION="${PROJECT_VERSION}")

target_link_libraries(smbii
        grrlib
        jpeg
        freetype
        brotlidec # needed because freetype is built with these now
        brotlicommon # needed because freetype is built with these now
        bz2
        fat
        asnd
        mad
        vorbis
        vorbisidec
        vorbisfile
        ogg
        png
        netkit::netkit
)

ogc_create_dol(smbii)