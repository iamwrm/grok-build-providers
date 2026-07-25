cmake_minimum_required(VERSION 3.20)
project(raylib_poc_hover LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

function(download_checked url destination sha256)
    set(needs_download TRUE)
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" existing_sha256)
        if(existing_sha256 STREQUAL sha256)
            set(needs_download FALSE)
        endif()
    endif()
    if(needs_download)
        file(DOWNLOAD "${url}" "${destination}"
            EXPECTED_HASH "SHA256=${sha256}"
            TLS_VERIFY ON
            SHOW_PROGRESS
        )
    endif()
endfunction()

set(FIRA_CODE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_fonts")
file(MAKE_DIRECTORY "${FIRA_CODE_DIR}")
download_checked(
    "https://raw.githubusercontent.com/google/fonts/main/ofl/firacode/FiraCode%5Bwght%5D.ttf"
    "${FIRA_CODE_DIR}/FiraCode.ttf"
    "9335b082b3c7850d98a64b584f3417f65355f3471278bb5eeb8c6c0e8657aeeb"
)
download_checked(
    "https://raw.githubusercontent.com/google/fonts/main/ofl/firacode/OFL.txt"
    "${FIRA_CODE_DIR}/OFL.txt"
    "926041dac670e6922505e35ac1661a4e8d20f1ffeabbbcb5edb5544370702369"
)

add_executable(raylib_poc_hover src/main.cpp)
target_link_libraries(raylib_poc_hover PRIVATE raylib)
add_custom_command(TARGET raylib_poc_hover POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${FIRA_CODE_DIR}"
        "$<TARGET_FILE_DIR:raylib_poc_hover>/assets/fonts"
)

if(MSVC)
    target_compile_options(raylib_poc_hover PRIVATE /W4 /permissive-)
else()
    target_compile_options(raylib_poc_hover PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
    target_link_libraries(raylib_poc_hover PRIVATE
        "-framework IOKit"
        "-framework Cocoa"
        "-framework OpenGL"
    )
endif()
