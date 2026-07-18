# Unit tests are added here as each user story's core logic is implemented
# (constitution Principle 4: test core logic, not UI/glue code).

add_executable(test_archivecomparator
    test_archivecomparator.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/crccomparator.cpp
)
target_include_directories(test_archivecomparator PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_archivecomparator PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_archivecomparator COMMAND test_archivecomparator)

add_executable(test_gerberdifftrigger
    test_gerberdifftrigger.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/gerberdifftrigger.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberfiletype.cpp
)
target_include_directories(test_gerberdifftrigger PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberdifftrigger PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_gerberdifftrigger COMMAND test_gerberdifftrigger)

# Parser front-half suite, ported from the gerber-parser-3b2 parts-bin
# worktree together with the sources it exercises (see the v2 design doc,
# docs/superpowers/specs/2026-07-18-gerber-render-v2-design.md).

add_executable(test_gerberfiletype
    test_gerberfiletype.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberfiletype.cpp
)
target_include_directories(test_gerberfiletype PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberfiletype PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_gerberfiletype COMMAND test_gerberfiletype)

add_executable(test_gerberformat
    test_gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
)
target_include_directories(test_gerberformat PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberformat PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_gerberformat COMMAND test_gerberformat)

add_executable(test_gerberexpression
    test_gerberexpression.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerberexpression PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberexpression PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_gerberexpression COMMAND test_gerberexpression)

add_executable(test_gerberaperture
    test_gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
)
target_include_directories(test_gerberaperture PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberaperture PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerberaperture COMMAND test_gerberaperture)

add_executable(test_gerbermacro
    test_gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerbermacro PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerbermacro PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerbermacro COMMAND test_gerbermacro)

add_executable(test_gerberparser
    test_gerberparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerberparser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberparser PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerberparser COMMAND test_gerberparser)
