cmake_minimum_required(VERSION 3.20)

set(TARGET ex01)
add_executable(${TARGET} main.cpp)

target_include_directories(ex01 PRIVATE
    /Users/ptran/src/concurrency/stdexec/include
    )

set_target_properties(${TARGET} PROPERTIES
    FOLDER Tools
    XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++20"
    CXX_STANDARD 20
    INSTALL_RPATH @executable_path/../lib
    )

install(TARGETS ${TARGET})
