cmake_minimum_required(VERSION 3.10)
project(bark_notify C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# Source files
set(SOURCES bark_notify.c)

# Executable - WIN32 sets the subsystem to Windows (no console)
add_executable(bark_notify WIN32 ${SOURCES})

# Libraries
if(WIN32)
    target_link_libraries(bark_notify winhttp shell32 user32)
    # Statically link C runtime on Windows (MinGW)
    if(MINGW)
        target_link_options(bark_notify PRIVATE -static -mwindows)
    endif()
endif()
