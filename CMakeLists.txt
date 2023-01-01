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