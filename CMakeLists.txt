cmake_minimum_required(VERSION 3.21)

project(KeyRouter VERSION 1.0.0 LANGUAGES CXX)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

# CommonLibSSE-NG 6.7.0 source tree (supports 1.6.1170 and 1.7.99)
if(DEFINED ENV{CommonLibSSEPath_NG} AND IS_DIRECTORY "$ENV{CommonLibSSEPath_NG}")
	set(COMMONLIB_DIR "$ENV{CommonLibSSEPath_NG}")
else()
	set(COMMONLIB_DIR "C:/dllmaker/CommonLibSSE-NG")
endif()
message(STATUS "Using CommonLibSSE-NG from: ${COMMONLIB_DIR}")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)

add_subdirectory("${COMMONLIB_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)

add_library(${PROJECT_NAME} SHARED
	src/Main.cpp
	src/Choices.cpp
	src/Diag.cpp
	src/Settings.cpp
	src/Keys.cpp
	src/KillSwitch.cpp
	src/Owners.cpp
	src/Scanner.cpp
	src/Poll.cpp
	src/Router.cpp
	src/Viewer.cpp
	src/Menu.cpp
	src/Fix.cpp
	src/Gate.cpp
	src/Quick.cpp
	src/PCH.h
	src/Choices.h
	src/Diag.h
	src/Settings.h
	src/Keys.h
	src/KillSwitch.h
	src/Owners.h
	src/Scanner.h
	src/Poll.h
	src/Router.h
	src/Viewer.h
	src/Menu.h
	src/Fix.h
	src/Gate.h
	src/Quick.h
	src/SKSEMenuFramework.h
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_include_directories(${PROJECT_NAME}
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(${PROJECT_NAME}
	PRIVATE
		UNICODE
		_UNICODE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
)

target_compile_options(${PROJECT_NAME}
	PRIVATE
		/permissive-
		/Zc:preprocessor
		/EHsc
		/utf-8
		/MP
		$<$<CONFIG:Release>:/Zi>
)

target_link_options(${PROJECT_NAME}
	PRIVATE
		$<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO>
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# dinput8/dxguid: DirectInput keyboard hooks (phase 3); psapi: list loaded SKSE plugin DLLs
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE dinput8 dxguid psapi)

# No automatic copy: the finished DLL and PDB stay in build\Release\
