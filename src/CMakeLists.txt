cmake_minimum_required(VERSION 3.13)

project(hello_android)

find_package(geometry_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

set(CMAKE_VERBOSE_MAKEFILE ON)

add_library(android-ros SHARED
  # JNI layer
  jni/jni_bridge.cc
  jni/jvm.cc

  # ROS interface
  ros/ros_interface.cc

  # Sensor base classes
  sensors/base/sensor.cc
  sensors/base/sensor_descriptor.cc
  sensors/sensors.cc

  # Sensor implementations
  sensors/impl/accelerometer_sensor.cc
  sensors/impl/barometer_sensor.cc
  sensors/impl/gyroscope_sensor.cc
  sensors/impl/illuminance_sensor.cc
  sensors/impl/magnetometer_sensor.cc

  # Sensor controllers
  sensors/controllers/accelerometer_sensor_controller.cc
  sensors/controllers/barometer_sensor_controller.cc
  sensors/controllers/gyroscope_sensor_controller.cc
  sensors/controllers/illuminance_sensor_controller.cc
  sensors/controllers/magnetometer_sensor_controller.cc

  # Camera base classes
  camera/base/camera_descriptor.cc
  camera/base/camera_device.cc
  camera/camera_manager.cc

  # Camera controller
  camera/controllers/camera_controller.cc

  # GPS location provider
  gps/base/gps_location_provider.cc

  # GPS controller
  gps/controllers/gps_controller.cc
)
target_link_libraries(android-ros PRIVATE
    android
    camera2ndk
    mediandk
    log
    rclcpp::rclcpp
    ${geometry_msgs_TARGETS}
    ${sensor_msgs_TARGETS})
target_include_directories(android-ros PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
# LTO to reduce APK size
set_property(TARGET android-ros PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)


# Workaround https://gitlab.kitware.com/cmake/cmake/-/issues/21772
# TODO(sloretz) only do this if CMake version is less than 3.20
STRING(REGEX REPLACE "-fuse-ld=gold" "" CMAKE_CXX_LINK_OPTIONS_IPO ${CMAKE_CXX_LINK_OPTIONS_IPO})

install(TARGETS android-ros DESTINATION lib)
