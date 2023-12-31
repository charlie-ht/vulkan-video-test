cmake_minimum_required(VERSION 3.20)
project(vvp)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_STANDARD 23)

set(VVP_SOURCES
    src/main.cpp
    src/vk_memory_allocator.cpp
)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)

set(VVP_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND VVP_INCLUDE_DIRS ${THIRD_PARTY_DIR}/Vulkan-Headers/include)
list(APPEND VVP_INCLUDE_DIRS ${THIRD_PARTY_DIR}/Vulkan-Headers/include/vulkan)
list(APPEND VVP_INCLUDE_DIRS ${THIRD_PARTY_DIR}/VulkanMemoryAllocator/include)
# list(APPEND VVP_INCLUDE_DIRS $ENV{HOME}/cts/root/include)

# Leaving here for future WSI integration
# add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
# find_package(SDL2 REQUIRED)
# list(APPEND VVP_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
# list(APPEND VVP_LIBRARIES SDL2::SDL2)
# set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
# set(IMGUI_SOURCES
#         ${IMGUI_DIR}/imgui.cpp
#         ${IMGUI_DIR}/imgui_draw.cpp
#         ${IMGUI_DIR}/imgui_demo.cpp
#         ${IMGUI_DIR}/imgui_tables.cpp
#         ${IMGUI_DIR}/imgui_widgets.cpp
#         ${IMGUI_DIR}/imgui.h
#         ${IMGUI_DIR}/imstb_rectpack.h
#         ${IMGUI_DIR}/imstb_textedit.h
#         ${IMGUI_DIR}/imstb_truetype.h
#         ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
#         ${IMGUI_DIR}/backends/imgui_impl_sdl2.h
#         ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
#         ${IMGUI_DIR}/backends/imgui_impl_vulkan.h
# )
#add_executable(vvp ${VVP_SOURCES} ${IMGUI_SOURCES})
#target_compile_definitions(vvp PRIVATE VK_USE_PLATFORM_XCB_KHR)
#target_include_directories(vvp SYSTEM PUBLIC ${VVP_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/third_party ${IMGUI_DIR} ${IMGUI_DIR}/backends)

add_executable(vvp ${VVP_SOURCES})
target_include_directories(vvp SYSTEM PUBLIC ${VVP_INCLUDE_DIRS})
target_link_libraries(vvp PRIVATE ${VVP_LIBRARIES})
