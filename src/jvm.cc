#include "jvm.h"

static JavaVM* g_java_vm = nullptr;

void sensors_for_ros::SetJavaVM(JavaVM* vm) {
  g_java_vm = vm;
}

JavaVM* sensors_for_ros::GetJavaVM() {
  return g_java_vm;
}
