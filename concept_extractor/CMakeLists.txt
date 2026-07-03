cmake_minimum_required(VERSION 3.22)
project(concept_extractor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  message(STATUS "ccache found: ${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
else()
  message(STATUS "ccache not found; builds may be slower")
endif()

option(CONCEPT_EXTRACTOR_NATIVE_ARCH "Enable -march=native/-mtune=native" OFF)
option(CONCEPT_EXTRACTOR_WERROR "Treat warnings as errors for project targets" ON)
option(BUILD_PYTHON "Build Python extension module" ON)

function(concept_extractor_enable_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
    if(CONCEPT_EXTRACTOR_WERROR)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    if(CONCEPT_EXTRACTOR_WERROR)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
    if(CONCEPT_EXTRACTOR_NATIVE_ARCH)
      target_compile_options(${target_name} PRIVATE -march=native -mtune=native)
    endif()
  endif()
endfunction()

set(CONCEPT_EXTRACTOR_STEMMER_RU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cpp/third_party/libstemmer_ru)
set(CONCEPT_EXTRACTOR_STEMMER_RU_SRC
  ${CONCEPT_EXTRACTOR_STEMMER_RU_DIR}/runtime/api.c
  ${CONCEPT_EXTRACTOR_STEMMER_RU_DIR}/runtime/utilities.c
  ${CONCEPT_EXTRACTOR_STEMMER_RU_DIR}/libstemmer/libstemmer_utf8.c
  ${CONCEPT_EXTRACTOR_STEMMER_RU_DIR}/src_c/stem_UTF_8_russian.c
)

add_library(concept_extractor_stemmer_ru STATIC ${CONCEPT_EXTRACTOR_STEMMER_RU_SRC})
set_target_properties(concept_extractor_stemmer_ru PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(concept_extractor_stemmer_ru
  PUBLIC
    ${CONCEPT_EXTRACTOR_STEMMER_RU_DIR}/include
)

set(CONCEPT_EXTRACTOR_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/cpp/src/graph/morph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cpp/src/graph/person_extractor.cpp
)

add_library(concept_extractor_lib STATIC ${CONCEPT_EXTRACTOR_SRC})
set_target_properties(concept_extractor_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(concept_extractor_lib PUBLIC cxx_std_23)
target_include_directories(concept_extractor_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
  PRIVATE
    ${CONCEPT_EXTRACTOR_STEMMER_RU_DIR}/include
)
target_link_libraries(concept_extractor_lib
  PRIVATE
    concept_extractor_stemmer_ru
)
concept_extractor_enable_warnings(concept_extractor_lib)

if(BUILD_PYTHON)
  list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 CONFIG REQUIRED)

  pybind11_add_module(_graph
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/bindings/graph.cpp
  )
  target_link_libraries(_graph
    PRIVATE
      concept_extractor_lib
      Python3::Module
  )
  set_target_properties(_graph PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
  concept_extractor_enable_warnings(_graph)

  install(TARGETS _graph
    LIBRARY DESTINATION "concept_extractor/graph"
    RUNTIME DESTINATION "concept_extractor/graph"
    ARCHIVE DESTINATION "concept_extractor/graph"
  )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Plain C ABI shared library (for cgo / Go bindings and other FFI consumers).
# Builds libconcept_extractor_c.{so,dylib,dll} that statically bundles the C++
# core + stemmer behind the extern "C" surface in cpp/bindings/graph_c.cpp.
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_C_API "Build the plain C ABI shared library" OFF)
if(BUILD_C_API)
  add_library(concept_extractor_c SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/bindings/graph_c.cpp
  )
  target_include_directories(concept_extractor_c
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/cpp/bindings
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
  )
  target_link_libraries(concept_extractor_c
    PRIVATE
      concept_extractor_lib
  )
  target_compile_definitions(concept_extractor_c PRIVATE CONCEPT_EXTRACTOR_C_BUILD)
  set_target_properties(concept_extractor_c PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
  concept_extractor_enable_warnings(concept_extractor_c)

  install(TARGETS concept_extractor_c
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
  )
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cpp/bindings/graph_c.h
    DESTINATION include
  )
endif()

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
  enable_testing()
  add_executable(concept_extractor_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/tests/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/tests/cpp/person_extractor_rules_test.cpp
  )
  target_link_libraries(concept_extractor_tests PRIVATE concept_extractor_lib)
  target_include_directories(concept_extractor_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/tests
  )
  concept_extractor_enable_warnings(concept_extractor_tests)
  add_test(NAME concept_extractor_tests COMMAND concept_extractor_tests)
endif()
