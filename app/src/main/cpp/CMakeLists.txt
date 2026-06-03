cmake_minimum_required(VERSION 3.22.1)

project(Winlator)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wno-unused-function -Wimplicit-function-declaration")

add_subdirectory(OpenXR-SDK)
add_subdirectory(patchelf)
add_subdirectory(adrenotools)

add_library(winlator SHARED
        xr/engine.c
        xr/framebuffer.c
        xr/input.c
        xr/main.c
        xr/math.c
        xr/renderer.c
        winlator/drawable.c
        winlator/gpu_image.c
        winlator/sysvshared_memory.c
        winlator/xconnector_epoll.c
        winlator/alsa_client.c
        winlator/vulkan.c
)

target_compile_options(winlator PRIVATE -Wall -Wextra)

target_link_libraries(winlator
        log
        android
        jnigraphics
        openxr_loader
        aaudio
        EGL
        GLESv2
        GLESv3
        adrenotools
)

add_library(touch_processor SHARED
        winlator/touch_processor.c
        winlator/touch_processor_actions.c
        winlator/touch_processor_element_shared.c
        winlator/touch_processor_button.c
        winlator/touch_processor_dpad.c
        winlator/touch_processor_stick.c
        winlator/touch_processor_trackpad.c
        winlator/touch_processor_gesture.c
        winlator/touch_processor_gesture_base.c
        winlator/touch_processor_touchscreen.c
        winlator/touch_processor_touchpad.c
        winlator/touch_processor_jni.cpp
)

target_compile_options(touch_processor PRIVATE -Wall -Wextra)
set_source_files_properties(winlator/touch_processor.c PROPERTIES COMPILE_OPTIONS "-std=c11")
set_source_files_properties(winlator/touch_processor_jni.cpp PROPERTIES COMPILE_OPTIONS "-std=c++17")
target_link_libraries(touch_processor log android)

add_library(fakeinput SHARED
        winlator/fakeinput.cpp
)

target_compile_options(fakeinput PRIVATE -Wall -Wextra -fvisibility=hidden)

target_link_libraries(fakeinput
        log
        dl
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/window_vert.h
    COMMAND glslangValidator -V --vn window_vert_code -x -o ${CMAKE_CURRENT_BINARY_DIR}/window_vert.h ${CMAKE_CURRENT_SOURCE_DIR}/winlator/window.vert
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/winlator/window.vert
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/window_frag.h
    COMMAND glslangValidator -V --vn window_frag_code -x -o ${CMAKE_CURRENT_BINARY_DIR}/window_frag.h ${CMAKE_CURRENT_SOURCE_DIR}/winlator/window.frag
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/winlator/window.frag
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(vulkan_renderer SHARED
        winlator/vulkan_jni.cpp
        winlator/VulkanRendererContext.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/window_vert.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_frag.h
)

target_compile_options(vulkan_renderer PRIVATE -Wall -Wextra -fvisibility=hidden -std=c++17)

target_link_libraries(vulkan_renderer
        log
        android
        vulkan
        adrenotools
        dl
        atomic
)