# Unit tests are added here as each user story's core logic is implemented
# (constitution Principle 4: test core logic, not UI/glue code).

add_executable(test_archivecomparator
    test_archivecomparator.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/crccomparator.cpp
)
target_include_directories(test_archivecomparator PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_archivecomparator PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_archivecomparator COMMAND test_archivecomparator)

add_executable(test_archivereader
    test_archivereader.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/archivereader.cpp
)
target_include_directories(test_archivereader PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_archivereader PRIVATE Qt6::Core Qt6::Test QuaZip::QuaZip)
add_test(NAME test_archivereader COMMAND test_archivereader)

add_executable(test_archivecomparison
    test_archivecomparison.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/archivecomparison.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/crccomparator.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/archivereader.cpp
)
target_include_directories(test_archivecomparison PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_archivecomparison PRIVATE Qt6::Core Qt6::Test QuaZip::QuaZip)
add_test(NAME test_archivecomparison COMMAND test_archivecomparison)

add_executable(test_archivetreemodel
    test_archivetreemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/archivetreemodel.cpp
)
target_include_directories(test_archivetreemodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_archivetreemodel PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_archivetreemodel COMMAND test_archivetreemodel)

add_executable(test_comparisonsummary
    test_comparisonsummary.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/comparisonsummary.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberfiletype.cpp # kind sniffing (Task 15)
)
target_include_directories(test_comparisonsummary PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_comparisonsummary PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_comparisonsummary COMMAND test_comparisonsummary)

# Header-only helper, so no .cpp of its own to compile in.
add_executable(test_archivedroputil
    test_archivedroputil.cpp
)
target_include_directories(test_archivedroputil PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_archivedroputil PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_archivedroputil COMMAND test_archivedroputil)

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

add_executable(test_gerberobjectpaths
    test_gerberobjectpaths.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberobjectpaths.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerberobjectpaths PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberobjectpaths PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerberobjectpaths COMMAND test_gerberobjectpaths)

add_executable(test_layermaskrenderer
    test_layermaskrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/layermaskrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberobjectpaths.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_layermaskrenderer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_layermaskrenderer PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_layermaskrenderer COMMAND test_layermaskrenderer)

add_executable(test_maskcompositor
    test_maskcompositor.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/maskcompositor.cpp
)
target_include_directories(test_maskcompositor PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_maskcompositor PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_maskcompositor COMMAND test_maskcompositor)

add_executable(test_gerberparser
    test_gerberparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberobjectpaths.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerberparser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberparser PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerberparser COMMAND test_gerberparser)

add_executable(test_excellonparser
    test_excellonparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/excellonparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
)
target_include_directories(test_excellonparser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_excellonparser PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_excellonparser COMMAND test_excellonparser)

# The re-export check: export-noise rules, both parsers' non-geometry line
# lists, order-independent object comparison, and the pair collapse.
add_executable(test_samedatacheck
    test_samedatacheck.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/samedatacheck.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/samedatadiagnosis.cpp
    ${CMAKE_SOURCE_DIR}/src/archivecompare/comparisonsummary.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/layerequivalence.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberfiletype.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/excellonparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberobjectpaths.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_samedatacheck PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_samedatacheck PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_samedatacheck COMMAND test_samedatacheck)

# Inspects the BUILT EXECUTABLE, not the source, for any linked networking
# library - see the test file's own header for why a source grep is not
# sufficient (Qt6::Pdf once pulled Qt6Network.dll in transitively without a
# single reference in src/). add_dependencies, because it can only inspect
# the application binary once that binary exists.
add_executable(test_nonetworkdependency
    test_nonetworkdependency.cpp
)
target_include_directories(test_nonetworkdependency PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_nonetworkdependency PRIVATE Qt6::Core Qt6::Test)
add_dependencies(test_nonetworkdependency PcbSupplierWorkflow)
add_test(NAME test_nonetworkdependency COMMAND test_nonetworkdependency)
