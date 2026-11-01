block(SCOPE_FOR VARIABLES)
  set(BUILD_GMOCK OFF)
  set(INSTALL_GTEST OFF)
  set(gtest_build_tests OFF)
  set(gtest_build_samples OFF)
  if(MSVC)
    set(gtest_force_shared_crt ON)
  endif()

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
    GIT_SUBMODULES ""
  )
  FetchContent_MakeAvailable(googletest)
endblock()

include(GoogleTest)
