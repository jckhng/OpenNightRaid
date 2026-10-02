add_executable(niteraid_tests
    main.cpp
    test_velocity_tables.cpp
    test_scoring.cpp
    test_aircraft_traits.cpp
    test_audio_mapping.cpp
    test_wave_scheduling.cpp
    test_original_random.cpp
    test_collision_bounds.cpp
    test_debris_collisions.cpp
    test_ground_retirement.cpp
    test_survivor_handoff.cpp
    test_gameplay_flow.cpp
    test_gameplay_clock.cpp
    test_input_schedule.cpp
    test_terminal_audio_timing.cpp
    test_object_type_flags.cpp
    test_presentation.cpp
    test_finale_budget.cpp
    test_pizza_presenter.cpp
    test_helicopter_presenter.cpp
    test_config_ntr_layout.cpp
    test_high_score_insertion.cpp
    test_control_panel.cpp
    test_exit_card.cpp
    test_ntr_asset_mapping.cpp
    test_hidden_features.cpp
    test_state_serializer.cpp
    ../src/game.cpp
    ../src/exit_card.cpp
    ../src/text_mode.cpp
    ../src/game_types.cpp
    ../src/input_schedule.cpp
    ../src/ntr_assets.cpp
    ../src/presentation.cpp
    ../src/state_serializer.cpp
    ../src/survivor_presenter.cpp
    ../src/finale_particles.cpp
    ../src/finale_presenter.cpp
    ../src/pizza_presenter.cpp
    ../src/scripted_presenter.cpp
    ../src/helicopter_presenter.cpp
)

target_include_directories(niteraid_tests
    PRIVATE
        ../include
        .
)

# The tests build only the Game-side translation units, so they don't
# need SDL.
target_compile_definitions(niteraid_tests PRIVATE NITERAID_TESTS_BUILD=1)

add_test(NAME niteraid_tests COMMAND niteraid_tests)

add_executable(niteraid_text_mode_probe text_mode_probe.cpp
    ../src/exit_card.cpp ../src/text_mode.cpp)
target_include_directories(niteraid_text_mode_probe PRIVATE ../include)

# Exercise the same collision/handoff contracts with enhanced-mode gameplay
# compiled in, without requiring a window or any proprietary game assets.
set(remake_game_test_sources ${NITERAID_RUNTIME_SOURCES})
list(FILTER remake_game_test_sources EXCLUDE REGEX "^(third_party/|src/(main|app|audio|renderer)\\.cpp$)")
list(TRANSFORM remake_game_test_sources PREPEND "../")
add_executable(niteraid_remake_game_tests main.cpp test_debris_collisions.cpp
    test_ground_retirement.cpp test_survivor_handoff.cpp test_config_ntr_layout.cpp
    ${remake_game_test_sources})
target_include_directories(niteraid_remake_game_tests PRIVATE ../include .)
target_compile_definitions(niteraid_remake_game_tests PRIVATE NITERAID_REMAKE=1 NITERAID_TESTS_BUILD=1)
add_test(NAME niteraid_remake_game_tests COMMAND niteraid_remake_game_tests)
# Non-CTest current faithful actor-state evidence probe.
set(wreckage_probe_sources ${NITERAID_RUNTIME_SOURCES})
list(FILTER wreckage_probe_sources EXCLUDE REGEX "^(third_party/|src/(main|app|audio|renderer)\\.cpp$)")
list(TRANSFORM wreckage_probe_sources PREPEND "../")
add_executable(niteraid_wreckage_state_probe wreckage_state_probe.cpp ${wreckage_probe_sources})
target_include_directories(niteraid_wreckage_state_probe PRIVATE ../include ../third_party/nuked-opl3)
target_compile_definitions(niteraid_wreckage_state_probe PRIVATE NITERAID_TESTS_BUILD=1)

if (NITERAID_SDL2_TARGET)
    set(input_test_sources ${NITERAID_RUNTIME_SOURCES})
    list(FILTER input_test_sources EXCLUDE REGEX "^(third_party/|src/(main|app|audio)\\.cpp$)")
    list(TRANSFORM input_test_sources PREPEND "../")
    add_executable(niteraid_input_tests main.cpp test_sdl_name_entry.cpp test_sdl_asset_origins.cpp
        test_sdl_text_mode.cpp ${input_test_sources})
    target_include_directories(niteraid_input_tests PRIVATE ../include .)
    target_compile_definitions(niteraid_input_tests PRIVATE NITERAID_ENABLE_SDL2=1 NITERAID_TESTS_BUILD=1)
    niteraid_link_sdl2(niteraid_input_tests)
    add_test(NAME niteraid_input_tests COMMAND niteraid_input_tests)
    set_tests_properties(niteraid_input_tests PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")

    add_executable(niteraid_ui_replay_probe ui_replay_probe.cpp ${input_test_sources})
    target_include_directories(niteraid_ui_replay_probe PRIVATE ../include .)
    target_compile_definitions(niteraid_ui_replay_probe PRIVATE NITERAID_ENABLE_SDL2=1 NITERAID_TESTS_BUILD=1)
    niteraid_link_sdl2(niteraid_ui_replay_probe)
endif()

# Explicit Tier 3 probe: requires a user-supplied archive, never a CTest asset.
if (NITERAID_SDL2_TARGET)
    add_executable(niteraid_audio_reset_probe
        audio_reset_probe.cpp
        ../src/audio.cpp
        ../src/game_types.cpp
        ../src/ntr_assets.cpp
        ../third_party/nuked-opl3/opl3.c
    )
    target_include_directories(niteraid_audio_reset_probe PRIVATE
        ../include ../third_party/nuked-opl3)
    target_compile_definitions(niteraid_audio_reset_probe PRIVATE NITERAID_ENABLE_SDL2=1)
    niteraid_link_sdl2(niteraid_audio_reset_probe)
endif()
