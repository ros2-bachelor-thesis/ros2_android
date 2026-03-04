#pragma once

#include <jni.h>

namespace sensors_for_ros {
void SetJavaVM(JavaVM* vm);
JavaVM* GetJavaVM();
}  // namespace sensors_for_ros
