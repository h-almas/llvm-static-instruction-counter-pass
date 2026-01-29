# if(LLVM_BYE_LINK_INTO_TOOLS)
#     message(WARNING "Setting LLVM_BYE_LINK_INTO_TOOLS=ON only makes sense for testing purpose")
# endif()
#
# # The plugin expects to not link against the Support and Core libraries,
# # but expects them to exist in the process loading the plugin. This doesn't
# # work with DLLs on Windows (where a shared library can't have undefined
# # references), so just skip this example on Windows.
# if (NOT WIN32 AND NOT CYGWIN)
#     add_llvm_pass_plugin(InstructionCount
#     InstructionCount.cpp
#     DEPENDS
#     intrinsics_gen
#     BUILDTREE_ONLY
#    )
#
#     install(TARGETS ${name} RUNTIME DESTINATION "${LLVM_EXAMPLES_INSTALL_DIR}")
#     set_target_properties(${name} PROPERTIES FOLDER "Examples")
# endif()

cmake_minimum_required(VERSION 3.20)
project(instruction-count-pass)

#===============================================================================
# 1. LOAD LLVM CONFIGURATION
#===============================================================================
# Set this to a valid LLVM installation dir
# set(LLVM_INSTALL_DIR "" CACHE PATH "LLVM installation directory")

# Add the location of LLVMConfig.cmake to CMake search paths (so that
# find_package can locate it)
list(APPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_DIR}/lib/cmake/llvm/")

find_package(LLVM CONFIG)
if("${LLVM_VERSION_MAJOR}" VERSION_LESS 20)
    message(FATAL_ERROR "Found LLVM ${LLVM_VERSION_MAJOR}, but need LLVM 20 or above")
endif()

# HelloWorld includes headers from LLVM - update the include paths accordingly
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

#===============================================================================
# 2. LLVM-TUTOR BUILD CONFIGURATION
#===============================================================================
# Use the same C++ standard as LLVM does
set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

#===============================================================================
# 3. ADD THE TARGET
#===============================================================================
add_library(InstructionCount SHARED InstructionCount.cpp)

# Allow undefined symbols in shared objects on Darwin (this is the default
# behaviour on Linux)
target_link_libraries(InstructionCount
  "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>")
