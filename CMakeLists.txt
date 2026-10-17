cmake_minimum_required(VERSION 4.1)
project(WingChanger)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
        nosboost_sdk
        GIT_REPOSITORY https://github.com/halilefe4228/NosSDK_-NosBoost-.git
        GIT_TAG main
)
FetchContent_MakeAvailable(nosboost_sdk)

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 691b89baae95d56df42a59b031c9d19685b7ab2f
)
FetchContent_MakeAvailable(imgui)

add_library(WingChanger SHARED
        WingChanger/WingChanger.cpp
        WingChanger/Wings.cpp
        WingChanger/Nos.cpp
        WingChanger/Library.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_include_directories(WingChanger PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${nosboost_sdk_SOURCE_DIR}/classes
        ${nosboost_sdk_SOURCE_DIR}/classes/enums
        ${nosboost_sdk_SOURCE_DIR}/classes/structs
        ${nosboost_sdk_SOURCE_DIR}/packets
        ${nosboost_sdk_SOURCE_DIR}/packets/sub
        ${imgui_SOURCE_DIR}
)

target_compile_options(WingChanger PRIVATE /EHsc /MP)
target_link_libraries(WingChanger PRIVATE windowscodecs ole32 shell32)

if(NOT DEFINED ENV{CI})
    set_target_properties(WingChanger PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "C:/Program Files (x86)/Nostale/mods"
    )
endif()
