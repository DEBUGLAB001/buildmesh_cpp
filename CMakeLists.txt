cmake_minimum_required(VERSION 3.20)
project(BuildMesh CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WIN32)
    set(PLATFORM_LIBS ws2_32 wsock32 crypt32)
else()
    set(PLATFORM_LIBS pthread)
endif()

# Core static library containing all service logic
add_library(core_services STATIC
    src/service/dependency_engine.cpp
    src/service/build_planner.cpp
    src/service/build_executor.cpp
)

# Main HTTP server executable
add_executable(server
    src/main.cpp
)
target_link_libraries(server PRIVATE core_services ${PLATFORM_LIBS})

# Automated native test suite executable
add_executable(challenge_tests
    tests/test_suite.cpp
)
target_link_libraries(challenge_tests PRIVATE core_services ${PLATFORM_LIBS})
