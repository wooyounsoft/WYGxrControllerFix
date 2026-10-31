cmake_minimum_required(VERSION 3.20)
project(WYGxrControllerFix CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# vrserver에 로드되므로 CRT 정적 링크
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.3
)
FetchContent_Declare(vigemclient
    GIT_REPOSITORY https://github.com/nefarius/ViGEmClient.git
    GIT_TAG b66d02d57e32cc8595369c53418b843e958649b4
)
FetchContent_MakeAvailable(minhook vigemclient)
# v1.3.3에는 CMake 타깃이 없어 직접 빌드
add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

add_library(VirtualPad STATIC VirtualPad/VirtualPad.cpp VirtualPad/VirtualPad.h)
target_include_directories(VirtualPad PUBLIC ${CMAKE_SOURCE_DIR}/VirtualPad)
target_link_libraries(VirtualPad PRIVATE ViGEmClient setupapi)

add_subdirectory(ControllerFixCore)
add_subdirectory(OpenVRDriver)

add_executable(EstimatorTest tests/EstimatorTest.cpp)
target_link_libraries(EstimatorTest PRIVATE ControllerFixCore)
add_subdirectory(OpenXRLayer)
