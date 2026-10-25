set(LAYER_NAME XR_APILAYER_WY_gxr_controllerfix)
set(LAYER_DEPLOY_DIR ${CMAKE_SOURCE_DIR}/deploy/openxr)

add_library(OpenXRLayer SHARED
    Layer.cpp
    LayerLog.cpp
    SpaceVelocityFixer.cpp
)
set_target_properties(OpenXRLayer PROPERTIES
    OUTPUT_NAME ${LAYER_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${LAYER_DEPLOY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${LAYER_DEPLOY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${LAYER_DEPLOY_DIR}
)
target_include_directories(OpenXRLayer PRIVATE ${CMAKE_SOURCE_DIR}/third_party/openxr/include)
target_compile_definitions(OpenXRLayer PRIVATE XR_NO_PROTOTYPES)
target_link_libraries(OpenXRLayer PRIVATE ControllerFixCore)

add_custom_command(TARGET OpenXRLayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/${LAYER_NAME}.json ${LAYER_DEPLOY_DIR}/${LAYER_NAME}.json
)
