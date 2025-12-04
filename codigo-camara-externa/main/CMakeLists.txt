idf_component_register(
    SRCS 
        "main_softap.c"
        "camera_driver/camera_driver.c"
        "webserver/webserver.c"
        "vision/vision.c"
        "vision/homography.c"
        "softap/softap.c"
        "ws_server/ws_server.c"
        "vision_task/vision_task.c"
        "test_detection.c"
        "test_homography.c"
    PRIV_REQUIRES 
        esp_wifi
        nvs_flash
        esp_http_server
        esp_netif
        esp_event
        esp_timer
        espressif__esp32-camera
        json
    INCLUDE_DIRS 
        "."
        "./opencv"
)

add_prebuilt_library(opencv_imgcodecs "opencv/libopencv_imgcodecs.a")
add_prebuilt_library(libpng "opencv/3rdparty/liblibpng.a")
add_prebuilt_library(libzlib "opencv/3rdparty/libzlib.a")
add_prebuilt_library(opencv_imgproc "opencv/libopencv_imgproc.a")
add_prebuilt_library(opencv_core "opencv/libopencv_core.a")
add_prebuilt_library(ade "opencv/libade.a")

set(IMGCODEC_DEP libpng libzlib)
target_link_libraries(opencv_imgcodecs INTERFACE ${IMGCODEC_DEP})

set(CORE_DEP libzlib)
target_link_libraries(opencv_core INTERFACE ${CORE_DEP})

set(OPENCV_DEP opencv_imgcodecs opencv_imgproc opencv_core)
target_link_libraries(${COMPONENT_LIB} ${OPENCV_DEP})