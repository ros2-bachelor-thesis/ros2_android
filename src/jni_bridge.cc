#include <jni.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "camera_descriptor.h"
#include "camera_manager.h"
#include "controllers/accelerometer_sensor_controller.h"
#include "controllers/barometer_sensor_controller.h"
#include "controllers/camera_controller.h"
#include "controllers/gyroscope_sensor_controller.h"
#include "controllers/illuminance_sensor_controller.h"
#include "controllers/magnetometer_sensor_controller.h"
#include "jvm.h"
#include "log.h"
#include "ros_interface.h"
#include "sensor_data_provider.h"
#include "sensors.h"

static JavaVM* g_jvm = nullptr;

class AndroidApp {
 public:
  AndroidApp(const std::string& cache_dir, const std::string& package_name)
      : cache_dir_(cache_dir),
        package_name_(package_name),
        sensors_(package_name) {
    LOGI("Initializing Sensors");
    sensors_.Initialize();

    for (auto& sensor : sensors_.GetSensors()) {
      if (ASENSOR_TYPE_LIGHT == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::IlluminanceSensorController>(
                static_cast<sensors_for_ros::IlluminanceSensor*>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_GYROSCOPE == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::GyroscopeSensorController>(
                static_cast<sensors_for_ros::GyroscopeSensor*>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_ACCELEROMETER == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::AccelerometerSensorController>(
                static_cast<sensors_for_ros::AccelerometerSensor*>(
                    sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_PRESSURE == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::BarometerSensorController>(
                static_cast<sensors_for_ros::BarometerSensor*>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_MAGNETIC_FIELD == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::MagnetometerSensorController>(
                static_cast<sensors_for_ros::MagnetometerSensor*>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      }
    }
  }

  ~AndroidApp() = default;

  void StartCameras() {
    if (started_cameras_) {
      return;
    }
    if (camera_manager_.HasCameras()) {
      started_cameras_ = true;
      LOGI("Starting cameras");
      std::vector<sensors_for_ros::CameraDescriptor> cameras =
          camera_manager_.GetCameras();
      for (auto cam_desc : cameras) {
        LOGI("Camera: %s", cam_desc.GetName().c_str());
        auto camera_controller =
            std::make_unique<sensors_for_ros::CameraController>(
                &camera_manager_, cam_desc, ros_);
        camera_controllers_.emplace_back(std::move(camera_controller));
      }
    }
  }

  void StartRos(int32_t ros_domain_id, const std::string& network_interface) {
    std::string cyclone_uri = cache_dir_;
    if (cyclone_uri.back() != '/') {
      cyclone_uri += '/';
    }
    cyclone_uri += "cyclonedds.xml";
    LOGI("Cyclonedds URI: %s", cyclone_uri.c_str());
    setenv("CYCLONEDDS_URI", cyclone_uri.c_str(), 1);

    std::ofstream config_file(cyclone_uri.c_str(), std::ofstream::trunc);
    config_file << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
    config_file << "<CycloneDDS xmlns=\"https://cdds.io/config\" "
                   "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                   "xsi:schemaLocation=\"https://cdds.io/config "
                   "https://raw.githubusercontent.com/eclipse-cyclonedds/"
                   "cyclonedds/master/etc/cyclonedds.xsd\">";
    config_file << "<Domain id=\"any\"><General><Interfaces>";
    config_file << "<NetworkInterface name=\"" << network_interface << "\"/>";
    config_file << "</Interfaces></General></Domain></CycloneDDS>";
    config_file.close();

    if (ros_.Initialized()) {
      LOGI("Shutting down ROS");
      ros_.Shutdown();
    }
    LOGI("Initializing ROS");
    ros_.Initialize(ros_domain_id);
  }

  std::string GetSensorListJson() {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < controllers_.size(); ++i) {
      auto* c = controllers_[i].get();
      if (i > 0) ss << ",";
      ss << "{\"uniqueId\":\"" << c->UniqueId() << "\""
         << ",\"prettyName\":\"" << c->PrettyName() << "\""
         << ",\"sensorName\":\"" << c->SensorName() << "\""
         << ",\"vendor\":\"" << c->SensorVendor() << "\""
         << ",\"topicName\":\"" << c->TopicName() << "\""
         << ",\"topicType\":\"" << c->TopicType() << "\""
         << ",\"type\":\"sensor\"}";
    }
    ss << "]";
    return ss.str();
  }

  std::string GetSensorDataJson(const std::string& unique_id) {
    for (auto& c : controllers_) {
      if (unique_id == c->UniqueId()) {
        return c->GetLastMeasurementJson();
      }
    }
    for (auto& c : camera_controllers_) {
      if (unique_id == c->UniqueId()) {
        return c->GetLastMeasurementJson();
      }
    }
    return "{}";
  }

  std::string GetCameraListJson() {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < camera_controllers_.size(); ++i) {
      auto* c = camera_controllers_[i].get();
      if (i > 0) ss << ",";
      auto [width, height] = c->GetResolution();
      ss << "{\"uniqueId\":\"" << c->UniqueId() << "\""
         << ",\"name\":\"" << c->GetCameraName() << "\""
         << ",\"enabled\":" << (c->IsEnabled() ? "true" : "false")
         << ",\"imageTopicName\":\"" << c->ImageTopicName() << "\""
         << ",\"imageTopicType\":\"" << c->ImageTopicType() << "\""
         << ",\"infoTopicName\":\"" << c->InfoTopicName() << "\""
         << ",\"infoTopicType\":\"" << c->InfoTopicType() << "\""
         << ",\"resolutionWidth\":" << width
         << ",\"resolutionHeight\":" << height << "}";
    }
    ss << "]";
    return ss.str();
  }

  void EnableCamera(const std::string& unique_id) {
    for (auto& c : camera_controllers_) {
      if (unique_id == c->UniqueId()) {
        c->EnableCamera();
        return;
      }
    }
  }

  void DisableCamera(const std::string& unique_id) {
    for (auto& c : camera_controllers_) {
      if (unique_id == c->UniqueId()) {
        c->DisableCamera();
        return;
      }
    }
  }

  std::string cache_dir_;
  std::string package_name_;
  sensors_for_ros::RosInterface ros_;
  sensors_for_ros::Sensors sensors_;

  std::vector<std::unique_ptr<sensors_for_ros::SensorDataProvider>>
      controllers_;
  std::vector<std::unique_ptr<sensors_for_ros::CameraController>>
      camera_controllers_;

  sensors_for_ros::CameraManager camera_manager_;
  bool started_cameras_ = false;
  std::vector<std::string> network_interfaces_;
};

static std::unique_ptr<AndroidApp> g_app;

// JNI function names: underscores in package name become _1
// com.github.sloretz.sensors_for_ros -> com_github_sloretz_sensors_1for_1ros

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  g_jvm = vm;
  sensors_for_ros::SetJavaVM(vm);
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeInit(
    JNIEnv* env, jobject /*thiz*/, jstring cache_dir, jstring package_name) {
  const char* cache_dir_c = env->GetStringUTFChars(cache_dir, nullptr);
  const char* package_name_c = env->GetStringUTFChars(package_name, nullptr);

  LOGI("nativeInit: cacheDir=%s packageName=%s", cache_dir_c, package_name_c);
  g_app = std::make_unique<AndroidApp>(
      std::string(cache_dir_c), std::string(package_name_c));

  env->ReleaseStringUTFChars(cache_dir, cache_dir_c);
  env->ReleaseStringUTFChars(package_name, package_name_c);
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeDestroy(
    JNIEnv* /*env*/, jobject /*thiz*/) {
  if (g_app) {
    LOGI("nativeDestroy");
    g_app->sensors_.Shutdown();
    g_app->ros_.Shutdown();
    g_app.reset();
  }
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeSetNetworkInterfaces(
    JNIEnv* env, jobject /*thiz*/, jobjectArray interfaces) {
  if (!g_app) return;

  std::vector<std::string> ifaces;
  jsize len = env->GetArrayLength(interfaces);
  for (jsize i = 0; i < len; ++i) {
    jstring jstr = (jstring)env->GetObjectArrayElement(interfaces, i);
    const char* str = env->GetStringUTFChars(jstr, nullptr);
    ifaces.emplace_back(str);
    env->ReleaseStringUTFChars(jstr, str);
  }

  g_app->network_interfaces_ = std::move(ifaces);
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeOnPermissionResult(
    JNIEnv* env, jobject /*thiz*/, jstring permission, jboolean granted) {
  if (!g_app) return;

  const char* perm = env->GetStringUTFChars(permission, nullptr);
  LOGI("Permission result: %s granted=%d", perm, (int)granted);

  if (std::string(perm) == "CAMERA" && granted) {
    g_app->StartCameras();
  }

  env->ReleaseStringUTFChars(permission, perm);
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeStartRos(
    JNIEnv* env, jobject /*thiz*/, jint domain_id, jstring network_interface) {
  if (!g_app) return;

  const char* iface = env->GetStringUTFChars(network_interface, nullptr);
  g_app->StartRos(domain_id, std::string(iface));
  env->ReleaseStringUTFChars(network_interface, iface);
}

JNIEXPORT jstring JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeGetSensorList(
    JNIEnv* env, jobject /*thiz*/) {
  if (!g_app) return env->NewStringUTF("[]");
  std::string json = g_app->GetSensorListJson();
  return env->NewStringUTF(json.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeGetSensorData(
    JNIEnv* env, jobject /*thiz*/, jstring unique_id) {
  if (!g_app) return env->NewStringUTF("{}");

  const char* id = env->GetStringUTFChars(unique_id, nullptr);
  std::string json = g_app->GetSensorDataJson(std::string(id));
  env->ReleaseStringUTFChars(unique_id, id);
  return env->NewStringUTF(json.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeGetCameraList(
    JNIEnv* env, jobject /*thiz*/) {
  if (!g_app) return env->NewStringUTF("[]");
  std::string json = g_app->GetCameraListJson();
  return env->NewStringUTF(json.c_str());
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeEnableCamera(
    JNIEnv* env, jobject /*thiz*/, jstring unique_id) {
  if (!g_app) return;

  const char* id = env->GetStringUTFChars(unique_id, nullptr);
  g_app->EnableCamera(std::string(id));
  env->ReleaseStringUTFChars(unique_id, id);
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeDisableCamera(
    JNIEnv* env, jobject /*thiz*/, jstring unique_id) {
  if (!g_app) return;

  const char* id = env->GetStringUTFChars(unique_id, nullptr);
  g_app->DisableCamera(std::string(id));
  env->ReleaseStringUTFChars(unique_id, id);
}

JNIEXPORT jstring JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeGetNetworkInterfaces(
    JNIEnv* env, jobject /*thiz*/) {
  if (!g_app) return env->NewStringUTF("[]");

  std::ostringstream ss;
  ss << "[";
  for (size_t i = 0; i < g_app->network_interfaces_.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "\"" << g_app->network_interfaces_[i] << "\"";
  }
  ss << "]";
  return env->NewStringUTF(ss.str().c_str());
}

}  // extern "C"
