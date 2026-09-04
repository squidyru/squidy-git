# Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

# Unit tests
foreach (suite IN ITEMS diffdocument gitparse gitredact graphlayout)
    add_executable(tst_${suite} tst_${suite}.cpp)
    target_link_libraries(tst_${suite} PRIVATE squidygit_core Qt6::Test)
    if (MSVC)
        target_compile_options(tst_${suite} PRIVATE /W4 /utf-8)
    else ()
        target_compile_options(tst_${suite} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
    add_test(NAME ${suite} COMMAND tst_${suite})
endforeach ()

# Platform contract tests do not need the Git/core library.
add_executable(tst_platformservices tst_platformservices.cpp)
target_link_libraries(tst_platformservices PRIVATE squidygit_platform Qt6::Test)
if (MSVC)
    target_compile_options(tst_platformservices PRIVATE /W4 /utf-8)
else ()
    target_compile_options(tst_platformservices PRIVATE -Wall -Wextra -Wpedantic)
endif ()
add_test(NAME platformservices COMMAND tst_platformservices)

# Git integration tests
foreach (suite IN ITEMS gitclient gitintegration)
    add_executable(tst_${suite} tst_${suite}.cpp)
    target_link_libraries(tst_${suite} PRIVATE squidygit_core Qt6::Test)
    if (MSVC)
        target_compile_options(tst_${suite} PRIVATE /W4 /utf-8)
    else ()
        target_compile_options(tst_${suite} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
    add_test(NAME ${suite} COMMAND tst_${suite})
    set_tests_properties(${suite} PROPERTIES LABELS integration)
endforeach ()

# Remote protocols, credential helper and cancellation. Needs the application
# itself, which doubles as the credential helper Git runs.
add_executable(tst_gitremote tst_gitremote.cpp)
target_link_libraries(tst_gitremote PRIVATE squidygit_core Qt6::Network Qt6::Test)
target_compile_definitions(tst_gitremote PRIVATE
        SQUIDYGIT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        SQUIDYGIT_APPLICATION_PATH="$<TARGET_FILE:SquidyGit>")
add_dependencies(tst_gitremote SquidyGit)
if (MSVC)
    target_compile_options(tst_gitremote PRIVATE /W4 /utf-8)
else ()
    target_compile_options(tst_gitremote PRIVATE -Wall -Wextra -Wpedantic)
endif ()
add_test(NAME gitremote COMMAND tst_gitremote)
set_tests_properties(gitremote PROPERTIES LABELS integration)

# Widget integration tests
foreach (suite IN ITEMS diffview filespage windowbackground icons mainwindow)
    add_executable(tst_${suite} tst_${suite}.cpp)
    target_link_libraries(tst_${suite} PRIVATE squidygit_ui Qt6::Test)
    if (MSVC)
        target_compile_options(tst_${suite} PRIVATE /W4 /utf-8)
    else ()
        target_compile_options(tst_${suite} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
    add_test(NAME ${suite} COMMAND tst_${suite})
    set_tests_properties(${suite} PROPERTIES
            LABELS integration
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach ()

add_executable(tst_repositoryview tst_repositoryview.cpp)
target_link_libraries(tst_repositoryview PRIVATE squidygit_ui Qt6::Test)
if (MSVC)
    target_compile_options(tst_repositoryview PRIVATE /W4 /utf-8)
else ()
    target_compile_options(tst_repositoryview PRIVATE -Wall -Wextra -Wpedantic)
endif ()
# Registered one function per entry rather than as a single run. Each gets its
# own process, so no state carries between them, and a failure names itself in
# the ctest summary even when the run leaves no output behind. Keep the list in
# step with the private slots of TestRepositoryView.
set(REPOSITORY_VIEW_CASES
        appliesFirstSnapshot
        refreshesAfterExternalIndexChange
        feedsTheHistoryViewFromTheModel
        countsIncomingCommitsWithoutUserAction
        checksEveryOpenTabAndNotJustTheActiveOne
        keepsTheInterfaceLiveWhileAnOperationRuns
        refusesToStartASecondOperation
        keepsEveryPaneReachable
        togglesSidebarSections
        paintsSidebarSelectionAsOneRow
        readsTheWorkingTreeDiffOffTheUiThread
        dropsADiffTheSelectionHasMovedPast
        listsCommitFilesWithoutBlocking)

foreach (case IN LISTS REPOSITORY_VIEW_CASES)
    add_test(NAME repositoryview.${case} COMMAND tst_repositoryview ${case})
    set_tests_properties(repositoryview.${case} PROPERTIES
            LABELS integration
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach ()
