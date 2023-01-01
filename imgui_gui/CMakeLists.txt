project(dust3d-gui)
add_executable(${PROJECT_NAME}
  main.cpp
  ${IMGUI_DEMO_SOURCES}
)
find_package(imgui CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)

#The package sfml provides CMake targets:

find_package(SFML COMPONENTS system window graphics CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-system sfml-network sfml-graphics sfml-window)

# If you want SFML to provide an implementation of main():
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-main)

find_package(ImGui-SFML CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ImGui-SFML::ImGui-SFML)


target_link_libraries(${PROJECT_NAME} PRIVATE dust3d)

target_compile_definitions(${PROJECT_NAME} PRIVATE TEST_DS3_FILE="${CMAKE_CURRENT_SOURCE_DIR}/aaa.ds3")