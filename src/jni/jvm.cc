#include "jni/jvm.h"

static JavaVM* g_java_vm = nullptr;

void ros2_android::SetJavaVM(JavaVM* vm) {
  g_java_vm = vm;
}

JavaVM* ros2_android::GetJavaVM() {
  return g_java_vm;
}

JNIEnv* ros2_android::GetJNIEnv() {
  if (!g_java_vm) {
    return nullptr;
  }

  JNIEnv* env = nullptr;
  jint result = g_java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

  if (result == JNI_EDETACHED) {
    // Thread not attached, attach it
    result = g_java_vm->AttachCurrentThread(&env, nullptr);
    if (result != JNI_OK) {
      return nullptr;
    }
  } else if (result != JNI_OK) {
    return nullptr;
  }

  return env;
}
