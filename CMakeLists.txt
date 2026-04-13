cmake_minimum_required(VERSION 3.12)

project(anroid_ros-superbuild)

include(ExternalProject)

set(minimum_target_sdk 33)

if(NOT ANDROID_HOME)
  set(ANDROID_HOME "$ENV{ANDROID_HOME}")
endif()
if(NOT ANDROID_HOME)
  message(FATAL_ERROR "ANDROID_HOME must be given")
endif()

file(GLOB android_toolchain
  LIST_DIRECTORIES false
  "${ANDROID_HOME}/ndk/*/build/cmake/android.toolchain.cmake"
  )
if(NOT android_toolchain)
  message(FATAL_ERROR "Failed to find android toolchain file - is ANDROID_HOME correct?")
endif()

set(ANDROID_ABI arm64-v8a)

set(android_cmake_args
  -DCMAKE_TOOLCHAIN_FILE=${android_toolchain}
  -DANDROID_ABI=${ANDROID_ABI}
  -DANDROID_PLATFORM=android-${minimum_target_sdk}
  )

# Make place to install Python packages we depend on
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/deps/_python_)

include(dependencies.cmake)

build_native_dependencies()

build_crosscompile_dependencies()

##################################
# Android specific projects
##################################


set(cmake_with_env "${CMAKE_COMMAND}" -E
  env
  "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/deps/_python_"
  "AMENT_PREFIX_PATH=${CMAKE_CURRENT_BINARY_DIR}/deps"
  "${CMAKE_COMMAND}")

ExternalProject_Add(ros_for_android
  BUILD_ALWAYS ON
  DOWNLOAD_COMMAND ""
  CMAKE_COMMAND ${cmake_with_env} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  BUILD_COMMAND ${cmake_with_env} --build .
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src"
  DEPENDS
  deps-rclcpp
  deps-rclcpp_lifecycle
  deps-std_msgs
  deps-sensor_msgs
  deps-vermin_collector_ros_msgs
  deps-libyuv
  deps-ros2_android_perception
  CMAKE_ARGS
  ${android_cmake_args}
  "-DCMAKE_FIND_ROOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/deps"
  -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/ros_for_android
  # -DCMAKE_VERBOSE_MAKEFILE=ON
  )

##################################
# Stage native libs for Gradle
##################################

set(JNILIBS_DIR "${CMAKE_CURRENT_BINARY_DIR}/jniLibs/${ANDROID_ABI}")
file(MAKE_DIRECTORY ${JNILIBS_DIR})

set(staged_libs)
macro(stage_lib from_target lib_path)
  if(NOT TARGET ${from_target})
    message(FATAL_ERROR "${from_target} isn't a target. There must be a typo")
  endif()

  get_filename_component(libname "${lib_path}" NAME)
  add_custom_command(
    OUTPUT "${JNILIBS_DIR}/${libname}"
    COMMENT "Staging ${lib_path} -> jniLibs/${ANDROID_ABI}/${libname}"
    DEPENDS
    "${from_target}"
    "${lib_path}"
    COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${lib_path}"
    "${JNILIBS_DIR}/"
  )
  list(APPEND staged_libs "${JNILIBS_DIR}/${libname}")
endmacro()

stage_lib(ros_for_android "ros_for_android/lib/libandroid-ros.so")
stage_lib(deps-cyclonedds "deps/lib/libddsc.so")
stage_lib(deps-rmw_dds_common "deps/lib/librmw_dds_common.so")
stage_lib(deps-libyaml_vendor "deps/lib/libyaml.so")
stage_lib(deps-libyuv "deps/lib/libyuv.so")
stage_lib(deps-ros2_android_perception "deps/lib/libros2_android_perception.so")

add_custom_target(stage_jniLibs ALL
  DEPENDS ${staged_libs})

# Stage model assets to app/src/main/assets/models/
set(ASSETS_MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/assets/models")
file(MAKE_DIRECTORY ${ASSETS_MODELS_DIR})

set(MODEL_FILES
  yolov9_s_pobed.ncnn.param
  yolov9_s_pobed.ncnn.bin
  osnet_ain_x1_0.ncnn.param
  osnet_ain_x1_0.ncnn.bin
)

set(staged_models)
foreach(model_file ${MODEL_FILES})
  add_custom_command(
    OUTPUT "${ASSETS_MODELS_DIR}/${model_file}"
    COMMENT "Staging model ${model_file} -> assets/models/"
    DEPENDS
    deps-ros2_android_perception
    "deps/share/perception/models/${model_file}"
    COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "deps/share/perception/models/${model_file}"
    "${ASSETS_MODELS_DIR}/"
  )
  list(APPEND staged_models "${ASSETS_MODELS_DIR}/${model_file}")
endforeach()

add_custom_target(stage_models ALL
  DEPENDS ${staged_models})
