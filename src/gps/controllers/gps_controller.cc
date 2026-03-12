#include "gps/controllers/gps_controller.h"

#include <cmath>
#include <sstream>

#include "core/sensor_data_callback_queue.h"
#include "jni/jvm.h"

namespace ros2_android {

GpsController::GpsController(GpsLocationProvider* provider, RosInterface& ros)
    : provider_(provider),
      publisher_(ros),
      SensorDataProvider(provider->UniqueId()) {
  provider->SetLocationCallback(std::bind(&GpsController::OnLocationUpdate,
                                          this, std::placeholders::_1));
  publisher_.SetTopic("gps");
}

void GpsController::OnLocationUpdate(const sensor_msgs::msg::NavSatFix& msg) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_msg_ = msg;
  }
  publisher_.Publish(msg);

  // Trigger callback to notify UI of new sensor data (throttled to 10 Hz)
  ros2_android::PostSensorDataUpdate(std::string(UniqueId()));
}

std::string GpsController::GetLastMeasurementJson() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if we have valid GPS data (non-zero coordinates)
  if (last_msg_.latitude == 0.0 && last_msg_.longitude == 0.0) {
    return "{}";  // No data yet
  }

  std::ostringstream ss;
  ss << "{\"values\":[" << last_msg_.latitude << "," << last_msg_.longitude
     << "," << last_msg_.altitude << "],"
     << "\"unit\":\"°\","  // degree symbol for lat/lon
     << "\"latitude\":" << last_msg_.latitude << ","
     << "\"longitude\":" << last_msg_.longitude << ","
     << "\"altitude\":" << last_msg_.altitude << ","
     << "\"accuracy\":" << std::sqrt(last_msg_.position_covariance[0]) << "}";
  return ss.str();
}

bool GpsController::GetLastMeasurement(jni::SensorReadingData& out_data) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Check if we have valid GPS data (non-zero coordinates)
  if (last_msg_.latitude == 0.0 && last_msg_.longitude == 0.0) {
    return false;  // No data yet
  }
  out_data.values = {last_msg_.latitude, last_msg_.longitude, last_msg_.altitude};
  out_data.unit = "°";
  return true;
}

std::string GpsController::PrettyName() const { return "GPS Location"; }

void GpsController::Enable() {
  publisher_.Enable();

  // Notify Kotlin layer to start GPS manager
  JNIEnv* env = GetJNIEnv();
  if (!env) {
    LOGE("Failed to get JNI environment for GPS enable");
    return;
  }

  jclass nativeBridgeClass =
      env->FindClass("com/github/mowerick/ros2/android/NativeBridge");
  if (!nativeBridgeClass) {
    LOGE("Failed to find NativeBridge class");
    return;
  }

  jmethodID onGpsEnableMethod =
      env->GetStaticMethodID(nativeBridgeClass, "onGpsEnable", "()V");
  if (!onGpsEnableMethod) {
    LOGE("Failed to find onGpsEnable method");
    env->DeleteLocalRef(nativeBridgeClass);
    return;
  }

  env->CallStaticVoidMethod(nativeBridgeClass, onGpsEnableMethod);
  env->DeleteLocalRef(nativeBridgeClass);

  LOGI("GPS enabled, notified Kotlin layer");
}

void GpsController::Disable() {
  publisher_.Disable();

  // Notify Kotlin layer to stop GPS manager
  JNIEnv* env = GetJNIEnv();
  if (!env) {
    LOGE("Failed to get JNI environment for GPS disable");
    return;
  }

  jclass nativeBridgeClass =
      env->FindClass("com/github/mowerick/ros2/android/NativeBridge");
  if (!nativeBridgeClass) {
    LOGE("Failed to find NativeBridge class");
    return;
  }

  jmethodID onGpsDisableMethod =
      env->GetStaticMethodID(nativeBridgeClass, "onGpsDisable", "()V");
  if (!onGpsDisableMethod) {
    LOGE("Failed to find onGpsDisable method");
    env->DeleteLocalRef(nativeBridgeClass);
    return;
  }

  env->CallStaticVoidMethod(nativeBridgeClass, onGpsDisableMethod);
  env->DeleteLocalRef(nativeBridgeClass);

  LOGI("GPS disabled, notified Kotlin layer");
}

}  // namespace ros2_android
