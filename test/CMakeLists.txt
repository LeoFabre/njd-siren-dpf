add_executable(test_siren_engine test_siren_engine.cpp)
target_include_directories(test_siren_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugin)
add_test(NAME siren_engine COMMAND test_siren_engine)
