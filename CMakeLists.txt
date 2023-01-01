cmake_minimum_required(VERSION 3.24)

# ------------- integrate vcpkg -------------------
# NOTE: This must be defined before the first project call
# The CMAKE_TOOLCHAIN_FILE variable is not used until the project() command is reached
if(DEFINED CACHE{CMAKE_TOOLCHAIN_FILE})
    message("---------CMAKE_TOOLCHAIN_FILE is cached")
else()
    message("---------integrating vcpkg")
    set(VCPKG_CMAKE_TOOLCHAIN_PILE ${CMAKE_CURRENT_SOURCE_DIR}/build/fetched/vcpkg/scripts/buildsystems/vcpkg.cmake)

    if(NOT EXISTS ${VCPKG_CMAKE_TOOLCHAIN_PILE})
        include(FetchContent)
        FetchContent_Declare(vcpkg
            GIT_REPOSITORY https://github.com/microsoft/vcpkg/
            GIT_TAG 2022.09.27
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/fetched/vcpkg
        )
        FetchContent_populate(vcpkg)
    endif()

    set(CMAKE_TOOLCHAIN_FILE ${VCPKG_CMAKE_TOOLCHAIN_PILE} CACHE FILEPATH "")
    set(VCPKG_INSTALLED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/fetched/vcpkg_installed CACHE FILEPATH "")
endif()

project(dust3d)

file(GLOB_RECURSE HEADERS "dust3d/*.h" "third_party/*.h" "third_party/*.hpp")
file(GLOB_RECURSE SOURCES "dust3d/*.cc" "third_party/.c")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HEADERS} ${SOURCES})
add_library(${PROJECT_NAME} ${HEADERS} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/earcut.hpp/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml-1.13)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_executable(dust3d-bin main.cc)
target_link_libraries(dust3d-bin PRIVATE ${PROJECT_NAME})

add_subdirectory(imgui_gui)