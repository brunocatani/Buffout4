cmake_minimum_required(VERSION 3.23)
set(NAME "Buffout4")
set(VERSION 1.38.3)

project(
	${NAME}
	VERSION ${VERSION}
	LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

include(cmake/common.cmake)

add_subdirectory(src)
include(cmake/packaging.cmake)

# https://gitlab.kitware.com/cmake/cmake/-/issues/24922#note_1371990
if(MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_IDE) # 17.6+
	# When using /std:c++latest, "Build ISO C++23 Standard Library Modules" defaults to "Yes".
	# Default to "No" instead.
	#
	# As of CMake 3.26.4, there isn't a way to control this property
	# (https://gitlab.kitware.com/cmake/cmake/-/issues/24922),
	# We'll use the MSBuild project system instead
	# (https://learn.microsoft.com/en-us/cpp/build/reference/vcxproj-file-structure)
	file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/Directory.Build.props" CONTENT [==[
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <BuildStlModules>false</BuildStlModules>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
]==] @ONLY)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(spdlog CONFIG REQUIRED)
  add_executable(BuffoutCrashReportTests tests/CrashReportTests.cpp)
  target_compile_features(BuffoutCrashReportTests PRIVATE cxx_std_23)
  target_compile_options(BuffoutCrashReportTests PRIVATE /EHa /utf-8)
  target_compile_definitions(BuffoutCrashReportTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(BuffoutCrashReportTests PRIVATE src)
  target_link_libraries(BuffoutCrashReportTests PRIVATE spdlog::spdlog)
  add_test(NAME Buffout.CrashReport COMMAND BuffoutCrashReportTests "${CMAKE_BINARY_DIR}/test-output")
endif()
