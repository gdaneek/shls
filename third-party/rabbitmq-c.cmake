block(SCOPE_FOR VARIABLES)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_STATIC_LIBS ON)
  set(BUILD_EXAMPLES OFF)
  set(BUILD_TOOLS OFF)
  set(BUILD_TOOLS_DOCS OFF)
  set(BUILD_API_DOCS OFF)
  set(BUILD_OSSFUZZ OFF)
  set(RUN_SYSTEM_TESTS OFF)
  set(ENABLE_SSL_SUPPORT ON)

  FetchContent_Declare(
    rabbitmq_c
    GIT_REPOSITORY https://github.com/alanxz/rabbitmq-c.git
    GIT_TAG 84b81cd97a1b5515d3d4b304796680da24c666d8 # v0.15.0
    GIT_SUBMODULES ""
  )
  FetchContent_MakeAvailable(rabbitmq_c)
endblock()
