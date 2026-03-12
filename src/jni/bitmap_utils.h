#pragma once

#include <jni.h>
#include <cstdint>

namespace ros2_android {
namespace jni {

/**
 * Create an Android Bitmap object from RGB24 data.
 *
 * @param env JNI environment
 * @param data RGB24 pixel data (3 bytes per pixel: R, G, B)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return jobject Bitmap object (android.graphics.Bitmap) or nullptr on failure
 */
jobject CreateBitmapFromRGB(JNIEnv* env,
                           const uint8_t* data,
                           int width,
                           int height);

} // namespace jni
} // namespace ros2_android
