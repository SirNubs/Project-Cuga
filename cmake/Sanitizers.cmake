# Opt-in instrumentation for native headless tests; normal builds are unchanged.
option(PSPRECOMP_SANITIZERS "Instrument native tests with AddressSanitizer and UBSan" OFF)
if(PSPRECOMP_SANITIZERS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING
       OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
       OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18")
        message(FATAL_ERROR "PSPRECOMP_SANITIZERS requires native Linux Clang 18 or newer")
    endif()
    # Set directory properties before creating runtime libraries and test targets,
    # including profile sources and vendor implementations compiled by tests.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer -fno-optimize-sibling-calls -O1 -g)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
endif()
