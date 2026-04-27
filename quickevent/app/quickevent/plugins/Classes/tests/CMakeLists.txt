find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(test_autodrawengine
    test_autodrawengine.cpp
    ../src/drawing/autodrawalgorithm.cpp
)

target_include_directories(test_autodrawengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/drawing
)

target_link_libraries(test_autodrawengine PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME test_autodrawengine COMMAND test_autodrawengine)
