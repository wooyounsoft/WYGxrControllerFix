add_library(ControllerFixCore STATIC
    VelocityEstimator.cpp
    Settings.cpp
    ReleaseBoost.cpp
    VdFrameFix.cpp
)
target_include_directories(ControllerFixCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
