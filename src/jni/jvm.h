#pragma once

#include <jni.h>

namespace ros2_android {
void SetJavaVM(JavaVM* vm);
JavaVM* GetJavaVM();
JNIEnv* GetJNIEnv();
}  // namespace ros2_android
