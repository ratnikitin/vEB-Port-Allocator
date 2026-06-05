cmake_minimum_required(VERSION 3.14)
project(veb_port_allocator VERSION 1.0.0 LANGUAGES C)

# ── Standards and compiler flags ─────────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Detect if GCC/Clang — add hardening flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O2
        -Wall -Wextra -Wpedantic
        -Wformat=2 -Wformat-security
        -Wno-unused-parameter
        # Hardening
        -fstack-protector-strong
        -D_FORTIFY_SOURCE=2
        -fPIC
    )
    add_link_options(
        -Wl,-z,relro
        -Wl,-z,now
    )
endif()

# ── Include directories ───────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# ── Core library: libveb ──────────────────────────────────────────────────
add_library(veb STATIC
    src/veb/veb.c
)
target_include_directories(veb PUBLIC include src/veb)

# ── Utility library: libportutils ─────────────────────────────────────────
add_library(portutils STATIC
    src/utils/log.c
    src/utils/config.c
)
target_include_directories(portutils PUBLIC include src/utils)

# ── Pool manager library: libportpool ────────────────────────────────────
add_library(portpool STATIC
    src/manager/pool.c
)
target_include_directories(portpool PUBLIC include src/manager)
target_link_libraries(portpool PUBLIC veb portutils pthread)

# ── Client library: libportclient ─────────────────────────────────────────
add_library(portclient STATIC
    src/client/client.c
)
target_include_directories(portclient PUBLIC include src/manager)
target_link_libraries(portclient PUBLIC portutils)

# ── Shared versions (for external linking) ────────────────────────────────
add_library(portclient_shared SHARED
    src/client/client.c
)
set_target_properties(portclient_shared PROPERTIES OUTPUT_NAME portclient)
target_include_directories(portclient_shared PUBLIC include src/manager)
target_link_libraries(portclient_shared PUBLIC portutils)

# ── Daemon: portd ─────────────────────────────────────────────────────────
add_executable(portd
    src/manager/main.c
    src/manager/server.c
)
target_include_directories(portd PRIVATE
    include src/manager src/utils
)
target_link_libraries(portd PRIVATE portpool portutils veb pthread)

# ── CLI tool: portcli ─────────────────────────────────────────────────────
add_executable(portcli
    src/cli/portcli.c
)
target_include_directories(portcli PRIVATE
    include src/manager src/utils
)
target_link_libraries(portcli PRIVATE portclient portutils)

# ── Tests ────────────────────────────────────────────────────────────────
enable_testing()

add_executable(test_veb tests/test_veb.c)
target_include_directories(test_veb PRIVATE include src/veb)
target_link_libraries(test_veb PRIVATE veb)

add_executable(test_manager tests/test_manager.c)
target_include_directories(test_manager PRIVATE include src/manager src/utils)
target_link_libraries(test_manager PRIVATE portpool portutils veb pthread)

add_executable(benchmark tests/benchmark.c)
target_include_directories(benchmark PRIVATE include src/manager src/utils)
target_link_libraries(benchmark PRIVATE portpool portutils veb pthread m)

add_test(NAME veb_unit       COMMAND test_veb)
add_test(NAME manager_unit   COMMAND test_manager)

# ── Install ──────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS portd portcli
        RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

install(TARGETS portclient portclient_shared veb portpool
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES
    include/veb.h
    include/port_manager.h
    include/client.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/portd)

install(FILES scripts/portd.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)

install(FILES scripts/sample_config.json
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/portd
        RENAME config.json)

# ── Summary ──────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "vEB Port Allocator ${PROJECT_VERSION}")
message(STATUS "  Compiler:   ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install to: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
