# mod-playerbots CMake hook (LLM bots feature).
#
# Included by modules/CMakeLists.txt after add_library(modules), via
#   include("${CMAKE_SOURCE_DIR}/modules/${SOURCE_MODULE}/${SOURCE_MODULE}.cmake" OPTIONAL)
# so the file name must match the directory this module is checked out as.
# Sources themselves are auto-globbed by CollectSourceFiles; this file only
# adds the extra link targets and include directories src/LlmBots needs.
#
# CMAKE_CURRENT_LIST_DIR is this module's directory, so the paths below survive
# being checked out under a different directory name.

# With -DMODULES=dynamic this module builds into its own shared library rather
# than the aggregate `modules` target, so apply the settings to whichever
# target actually holds the sources.
get_filename_component(LLM_MODULE_DIR_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
string(TOLOWER "mod_${LLM_MODULE_DIR_NAME}" LLM_MODULE_DYNAMIC_TARGET)
if (TARGET ${LLM_MODULE_DYNAMIC_TARGET})
  set(LLM_MODULE_TARGET ${LLM_MODULE_DYNAMIC_TARGET})
else()
  set(LLM_MODULE_TARGET modules)
endif()

# HTTPS client for the LLM providers: Boost.Beast (header-only) + OpenSSL.
# The `openssl` interface target is defined by deps/openssl/CMakeLists.txt in
# the core build; Boost::boost carries the Beast headers.
target_link_libraries(${LLM_MODULE_TARGET}
  PRIVATE
    openssl)

if (TARGET Boost::boost)
  target_link_libraries(${LLM_MODULE_TARGET}
    PRIVATE
      Boost::boost)
endif()

# Vendored header-only libraries (nlohmann/json).
target_include_directories(${LLM_MODULE_TARGET}
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/deps)
