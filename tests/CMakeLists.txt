# Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>

# Unit tests
foreach (suite IN ITEMS diffdocument gitparse graphlayout)
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
add_executable(tst_gitclient tst_gitclient.cpp)
target_link_libraries(tst_gitclient PRIVATE squidygit_core Qt6::Test)
if (MSVC)
    target_compile_options(tst_gitclient PRIVATE /W4 /utf-8)
else ()
    target_compile_options(tst_gitclient PRIVATE -Wall -Wextra -Wpedantic)
endif ()
add_test(NAME gitclient COMMAND tst_gitclient)
set_tests_properties(gitclient PROPERTIES LABELS integration)

# Widget integration tests
foreach (suite IN ITEMS diffview filespage)
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
add_test(NAME repositoryview COMMAND tst_repositoryview)
set_tests_properties(repositoryview PROPERTIES
        LABELS integration
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
