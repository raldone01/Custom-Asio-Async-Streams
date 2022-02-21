cmake_minimum_required(VERSION 3.16)
project(CustomAsioStreams)

set(CMAKE_CXX_STANDARD 20)
MESSAGE(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

set(FETCHCONTENT_QUIET OFF)
set(CPM_USE_NAMED_CACHE_DIRECTORIES ON)
include(cmake/CPM.cmake)

# set CPM_SOURCE_CACHE env var
CPMAddPackage(
        NAME Boost
        VERSION 1.78.0
        GITHUB_REPOSITORY "boostorg/boost"
        GIT_TAG "boost-1.78.0"
)
find_package (Threads REQUIRED)



add_executable(IteratorConcepts src/IteratorConcepts.cpp)

add_executable(WorkGuards src/work_guards.cpp)
target_compile_definitions(WorkGuards PRIVATE BOOST_ASIO_NO_DEPRECATED)
target_link_libraries(WorkGuards PRIVATE Threads::Threads)
target_link_libraries(WorkGuards PRIVATE Boost::asio Boost::thread)

add_executable(CustomAsioStreamsExe main.cpp)
target_include_directories(CustomAsioStreamsExe PUBLIC src/)
target_compile_definitions(CustomAsioStreamsExe PRIVATE BOOST_ASIO_NO_DEPRECATED)
target_link_libraries(CustomAsioStreamsExe PRIVATE Threads::Threads)
target_link_libraries(CustomAsioStreamsExe PRIVATE Boost::beast Boost::thread)
if(WIN32)
    target_link_libraries(CustomAsioStreamsExe PRIVATE wsock32 ws2_32)
endif()

IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
    if(NOT WIN32)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer -ggdb3 -O0) # clang -fcoroutines-ts -stdlib=libstdc++
        add_link_options(-fsanitize=address)
        target_link_libraries(CustomAsioStreamsExe asan)
    endif()
ENDIF(CMAKE_BUILD_TYPE MATCHES DEBUG)
