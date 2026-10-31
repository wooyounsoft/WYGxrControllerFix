set(DRIVER_NAME wygxrcontrollerfix)
set(DEPLOY_DIR ${CMAKE_SOURCE_DIR}/deploy/${DRIVER_NAME})

add_library(OpenVRDriver SHARED
    DriverMain.cpp
    Hooks.cpp
    PoseFixer.cpp
    UevrDetector.cpp
    VdShmDump.cpp
)
set_target_properties(OpenVRDriver PROPERTIES
    OUTPUT_NAME driver_${DRIVER_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${DEPLOY_DIR}/bin/win64
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${DEPLOY_DIR}/bin/win64
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${DEPLOY_DIR}/bin/win64
)
target_include_directories(OpenVRDriver PRIVATE ${CMAKE_SOURCE_DIR}/third_party/openvr)
target_link_libraries(OpenVRDriver PRIVATE ControllerFixCore VirtualPad minhook)

# SteamVR 드라이버 폴더 구조로 리소스 복사
add_custom_command(TARGET OpenVRDriver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${DEPLOY_DIR}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/driver.vrdrivermanifest ${DEPLOY_DIR}/driver.vrdrivermanifest
)
