# Drogon otherwise silently omits Redis support if hiredis is missing.
find_path(HIREDIS_INCLUDE_DIR NAMES hiredis/hiredis.h REQUIRED)
find_library(HIREDIS_LIBRARY NAMES hiredis REQUIRED)

block(SCOPE_FOR VARIABLES)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_CTL OFF)
  set(BUILD_EXAMPLES OFF)
  set(BUILD_DOC OFF)
  set(BUILD_BROTLI OFF)
  set(BUILD_YAML_CONFIG OFF)
  set(BUILD_ORM ON)
  set(BUILD_POSTGRESQL ON)
  set(BUILD_REDIS ON)
  set(BUILD_MYSQL OFF)
  set(BUILD_SQLITE OFF)
  set(USE_SUBMODULE ON)

  FetchContent_Declare(
    drogon
    GIT_REPOSITORY https://github.com/drogonframework/drogon.git
    GIT_TAG a22956b82b6b221ceeff83913c3014ce0d048555 # v1.9.11
    GIT_SUBMODULES trantor
    GIT_SUBMODULES_RECURSE ON
  )
  FetchContent_MakeAvailable(drogon)

  # Drogon sets these in its own directory scope, not in ours.
  get_directory_property(drogon_pg_found DIRECTORY "${drogon_SOURCE_DIR}" DEFINITION pg_FOUND)
  get_directory_property(drogon_hiredis_found DIRECTORY "${drogon_SOURCE_DIR}" DEFINITION Hiredis_FOUND)
  if(NOT drogon_pg_found OR NOT drogon_hiredis_found)
    message(FATAL_ERROR "Drogon requires both PostgreSQL and Redis client support.")
  endif()
endblock()

# Upstream provides this namespace only in its installed package.
if(NOT TARGET Drogon::Drogon)
  add_library(Drogon::Drogon ALIAS drogon)
endif()
