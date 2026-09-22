cmake_minimum_required(VERSION 3.24)
project(CopilotAI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COPILOT_BUILD_PLUGIN "Build the X-Plane plugin" OFF)
set(XPLANE_SDK "" CACHE PATH "Path to extracted X-Plane SDK/SDK directory")

find_package(Threads REQUIRED)

add_library(copilot_common STATIC
    src/common/TcpSocket.cpp
)
target_include_directories(copilot_common PUBLIC include)
target_link_libraries(copilot_common PUBLIC
    Threads::Threads
)
if(WIN32)
    target_link_libraries(copilot_common PUBLIC ws2_32)
endif()
set_target_properties(copilot_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(CopilotCore
    src/core/main.cpp
)
target_link_libraries(CopilotCore PRIVATE copilot_common)

if(COPILOT_BUILD_PLUGIN)
    if(NOT XPLANE_SDK)
        message(FATAL_ERROR "COPILOT_BUILD_PLUGIN=ON requires -DXPLANE_SDK=/path/to/SDK")
    endif()

    add_library(CopilotPlugin MODULE
        src/plugin/PluginMain.cpp
        src/plugin/PluginIpcServer.cpp
    )

    target_include_directories(CopilotPlugin PRIVATE
        include
        src/plugin
        "${XPLANE_SDK}/CHeaders/XPLM"
    )

    target_compile_definitions(CopilotPlugin PRIVATE
        XPLM200=1
        XPLM210=1
        XPLM300=1
    )

    target_link_libraries(CopilotPlugin PRIVATE copilot_common)

    if(WIN32)
        target_compile_definitions(CopilotPlugin PRIVATE IBM=1 APL=0 LIN=0)
        target_link_libraries(CopilotPlugin PRIVATE
            "${XPLANE_SDK}/Libraries/Win/XPLM_64.lib"
        )
    elseif(APPLE)
        target_compile_definitions(CopilotPlugin PRIVATE IBM=0 APL=1 LIN=0)
        target_link_options(CopilotPlugin PRIVATE
            "-F${XPLANE_SDK}/Libraries/Mac"
            "-framework" "XPLM"
            "-undefined" "dynamic_lookup"
        )
    else()
        target_compile_definitions(CopilotPlugin PRIVATE IBM=0 APL=0 LIN=1)
        target_compile_options(CopilotPlugin PRIVATE -fvisibility=hidden)
    endif()

    set_target_properties(CopilotPlugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "CopilotAI"
        SUFFIX ".xpl"
    )
endif()
