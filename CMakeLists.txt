cmake_minimum_required(VERSION 3.25)

project(VGPU98 VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# The BIOS is assembled to a flat binary with custom commands, so only the executable is needed.
find_program(NASM_EXECUTABLE nasm REQUIRED)

enable_testing()

# Generated files (BIOS include files, the 86Box header) go here, never into the source tree.
set(VGPU98_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${VGPU98_GEN_DIR})

# The build stamp in every driver's log lines (cmake/build-stamp.cmake),
# checked at every build and rewritten only when it changes. The release and
# the reproducibility check set both values for every build they make.
set(VGPU98_BUILD_STAMP "" CACHE STRING "The drivers' build stamp (default: the short commit, -dirty with uncommitted changes)")
set(VGPU98_BUILD_TIME "" CACHE STRING "The commit's time in seconds since 1970, for the package's ISO image (default: the commit's)")
add_custom_target(build_stamp
    BYPRODUCTS ${VGPU98_GEN_DIR}/stamp.h ${VGPU98_GEN_DIR}/stamp.inc ${VGPU98_GEN_DIR}/stamp.txt
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_SOURCE_DIR} -DOUT=${VGPU98_GEN_DIR}
            -DSTAMP=${VGPU98_BUILD_STAMP} -DTIME=${VGPU98_BUILD_TIME} -P ${CMAKE_SOURCE_DIR}/cmake/build-stamp.cmake
    COMMENT "Checking the build stamp"
    VERBATIM)

# The 32-bit guest DLLs (driver32/, glide/): the host's gcc in 32-bit
# freestanding mode, for the Pentium II Windows 98 runs on (no MMX or SSE),
# with driver32/include's own freestanding headers.
set(VGPU98_DLL_CFLAGS
    -m32 -march=i686 -mno-mmx -mno-sse -mfpmath=387
    -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2
    -ffreestanding -fno-strict-aliasing -fno-asynchronous-unwind-tables -fno-ident
    -fno-stack-protector -mno-stack-arg-probe -fno-common
    -std=gnu11 -O2 -Wall -Wextra -Werror
    -nostdinc -isystem ${CMAKE_SOURCE_DIR}/driver32/include)

# The 86Box clone (branch vgpu98) that receives the generated device header.
set(VGPU98_86BOX_DIR ${CMAKE_SOURCE_DIR}/86Box CACHE PATH "86Box source tree")

add_subdirectory(tools)
add_subdirectory(card)
add_subdirectory(fonts)
add_subdirectory(bios)
add_subdirectory(driver)
add_subdirectory(driver32)
add_subdirectory(glide)
add_subdirectory(opengl)
add_subdirectory(cpl)
add_subdirectory(minivdd)
add_subdirectory(package)
