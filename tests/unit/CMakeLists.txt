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
