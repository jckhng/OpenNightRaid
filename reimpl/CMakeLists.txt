cmake_minimum_required(VERSION 3.21)

project(niteraid_remake LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    # Link our CRT statically; a supplied SDL DLL may still require its own CRT.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_definitions(NOMINMAX)
endif()

option(NITERAID_PORTMASTER "Build the aarch64 PortMaster/muOS target" OFF)
option(NITERAID_BUILD_TESTS "Build the niteraid_tests Tier 1 unit harness" ON)
option(NITERAID_BUILD_FAITHFUL "Build the faithful reimplementation executable" ON)

set(NITERAID_COPY_ASSETS_DEFAULT OFF)
find_package(SDL2 CONFIG QUIET)
if (TARGET SDL2::SDL2)
    set(NITERAID_SDL2_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()
    if (TARGET PkgConfig::SDL2)
        set(NITERAID_SDL2_TARGET PkgConfig::SDL2)
    else()
        message(FATAL_ERROR
            "SDL2 is required. Install SDL2 development files (vcpkg sdl2:x64-windows on Windows or the SDL2 pkg-config package on Linux), then reconfigure."
        )
    endif()
endif()
option(NITERAID_COPY_ASSETS "Copy imported assets beside the built executable" ${NITERAID_COPY_ASSETS_DEFAULT})

function(niteraid_link_sdl2 target)
    target_link_libraries(${target} PRIVATE ${NITERAID_SDL2_TARGET})
    if (TARGET SDL2::SDL2main)
        target_link_libraries(${target} PRIVATE SDL2::SDL2main)
    endif()
    target_compile_definitions(${target} PRIVATE NITERAID_ENABLE_SDL2=1)
endfunction()

set(NITERAID_RUNTIME_SOURCES
    third_party/nuked-opl3/opl3.c
    src/audio.cpp
    src/app.cpp
    src/exit_card.cpp
    src/text_mode.cpp
    src/game.cpp
    src/game_types.cpp
    src/input_schedule.cpp
    src/main.cpp
    src/ntr_assets.cpp
    src/presentation.cpp
    src/renderer.cpp
    src/state_serializer.cpp
    src/survivor_presenter.cpp
    src/finale_particles.cpp
    src/finale_presenter.cpp
    src/pizza_presenter.cpp
    src/scripted_presenter.cpp
    src/helicopter_presenter.cpp
)

add_executable(niteraid_remake ${NITERAID_RUNTIME_SOURCES})

target_include_directories(niteraid_remake
    PRIVATE
        include
        third_party/nuked-opl3
)

target_compile_definitions(niteraid_remake PRIVATE NITERAID_REMAKE=1)

if (NITERAID_COPY_ASSETS)
    add_custom_command(TARGET niteraid_remake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets
                $<TARGET_FILE_DIR:niteraid_remake>/assets
    )
endif()

niteraid_link_sdl2(niteraid_remake)
if (NITERAID_PORTMASTER)
    target_compile_definitions(niteraid_remake PRIVATE NITERAID_PORTMASTER=1)
endif()

if (NITERAID_BUILD_FAITHFUL)
    add_executable(niteraid ${NITERAID_RUNTIME_SOURCES})
    target_include_directories(niteraid
        PRIVATE
            include
            third_party/nuked-opl3
    )

    if (NITERAID_COPY_ASSETS)
        add_custom_command(TARGET niteraid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets
                    $<TARGET_FILE_DIR:niteraid>/assets
        )
    endif()

    niteraid_link_sdl2(niteraid)
endif()

if (NITERAID_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
