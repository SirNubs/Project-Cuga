# Opt-in source-based coverage; keep production and sanitizer builds separate.
option(PSPRECOMP_COVERAGE "Instrument native tests for LLVM source coverage" OFF)
if(PSPRECOMP_COVERAGE)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$"
       OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        message(FATAL_ERROR "PSPRECOMP_COVERAGE requires native Linux/macOS Clang")
    endif()
    if(PSPRECOMP_SANITIZERS)
        message(FATAL_ERROR "Use separate build directories for coverage and sanitizers")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping -O0 -g)
    add_link_options(-fprofile-instr-generate)
endif()
