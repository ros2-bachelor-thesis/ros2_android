#include <jni.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "camera/base/camera_descriptor.h"
#include "camera/camera_manager.h"
#include "camera/controllers/camera_controller.h"
#include "core/log.h"
#include "core/notification_queue.h"
#include "gps/base/gps_location_provider.h"
#include "gps/controllers/gps_controller.h"
#include "jni/jvm.h"
#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"
#include "sensors/controllers/accelerometer_sensor_controller.h"
#include "sensors/controllers/barometer_sensor_controller.h"
#include "sensors/controllers/gyroscope_sensor_controller.h"
#include "sensors/controllers/illuminance_sensor_controller.h"
#include "sensors/controllers/magnetometer_sensor_controller.h"
#include "sensors/sensors.h"

static JavaVM *g_jvm = nullptr;
static jobject g_notification_callback_object = nullptr;
static jmethodID g_notification_callback_method = nullptr;

class AndroidApp
{
public:
  AndroidApp(const std::string &cache_dir, const std::string &package_name)
      : cache_dir_(cache_dir),
        package_name_(package_name),
        sensors_(package_name)
  {
    LOGI("Initializing Sensors");
    sensors_.Initialize();

    for (auto &sensor : sensors_.GetSensors())
    {
      if (ASENSOR_TYPE_LIGHT == sensor->Descriptor().type)
      {
        auto controller =
            std::make_unique<ros2_android::IlluminanceSensorController>(
                static_cast<ros2_android::IlluminanceSensor *>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      }
      else if (ASENSOR_TYPE_GYROSCOPE == sensor->Descriptor().type)
      {
        auto controller =
            std::make_unique<ros2_android::GyroscopeSensorController>(
                static_cast<ros2_android::GyroscopeSensor *>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      }
      else if (ASENSOR_TYPE_ACCELEROMETER == sensor->Descriptor().type)
      {
        auto controller =
            std::make_unique<ros2_android::AccelerometerSensorController>(
                static_cast<ros2_android::AccelerometerSensor *>(
                    sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      }
      else if (ASENSOR_TYPE_PRESSURE == sensor->Descriptor().type)
      {
        auto controller =
            std::make_unique<ros2_android::BarometerSensorController>(
                static_cast<ros2_android::BarometerSensor *>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      }
      else if (ASENSOR_TYPE_MAGNETIC_FIELD == sensor->Descriptor().type)
      {
        auto controller =
            std::make_unique<ros2_android::MagnetometerSensorController>(
                static_cast<ros2_android::MagnetometerSensor *>(sensor.get()),
                ros_);
        controllers_.emplace_back(std::move(controller));
      }
    }

    // Initialize GPS location provider and controller
    LOGI("Initializing GPS");
    gps_provider_ = std::make_unique<ros2_android::GpsLocationProvider>();
    auto gps_controller = std::make_unique<ros2_android::GpsController>(
        gps_provider_.get(), ros_);
    controllers_.emplace_back(std::move(gps_controller));
  }

  ~AndroidApp() = default;

  void StartCameras()
  {
    if (started_cameras_)
    {
      return;
    }
    if (camera_manager_.HasCameras())
    {
      started_cameras_ = true;
      LOGI("Starting cameras");
      std::vector<ros2_android::CameraDescriptor> cameras =
          camera_manager_.GetCameras();
      for (auto cam_desc : cameras)
      {
        LOGI("Camera: %s", cam_desc.GetName().c_str());
        auto camera_controller =
            std::make_unique<ros2_android::CameraController>(
                &camera_manager_, cam_desc, ros_);
        camera_controllers_.emplace_back(std::move(camera_controller));
      }
    }
  }

  void StartRos(int32_t ros_domain_id, const std::string &network_interface)
  {
    std::string cyclone_uri = cache_dir_;
    if (cyclone_uri.back() != '/')
    {
      cyclone_uri += '/';
    }
    cyclone_uri += "cyclonedds.xml";
    LOGI("Setting CYCLONEDDS_URI: %s", cyclone_uri.c_str());
    LOGI("Setting ROS_DOMAIN_ID: %d", ros_domain_id);

    setenv("CYCLONEDDS_URI", cyclone_uri.c_str(), 1);
    setenv("ROS_DOMAIN_ID", std::to_string(ros_domain_id).c_str(), 1);

    std::ofstream config_file(cyclone_uri.c_str(), std::ofstream::trunc);
    if (!config_file.is_open())
    {
      LOGE("Failed to create cyclonedds.xml at %s", cyclone_uri.c_str());
      return;
    }

    config_file << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
    config_file << "<CycloneDDS xmlns=\"https://cdds.io/config\">\n";
    config_file << "  <Domain id=\"any\">\n";
    config_file << "    <General>\n";
    config_file << "      <AllowMulticast>true</AllowMulticast>\n";
    config_file << "      <Interfaces>\n";
    config_file << "        <NetworkInterface name=\"" << network_interface << "\"/>\n";
    config_file << "      </Interfaces>\n";
    config_file << "    </General>\n";
    config_file << "  </Domain>\n";
    config_file << "</CycloneDDS>\n";
    config_file.close();

    LOGI("Generated CycloneDDS config for interface: %s, domain: %d",
         network_interface.c_str(), ros_domain_id);

    if (ros_.Initialized())
    {
      LOGI("Shutting down ROS");
      ros_.Shutdown();
    }
    LOGI("Initializing ROS");
    ros_.Initialize(ros_domain_id);
  }

  void StopRos()
  {
    LOGI("Stopping ROS - disabling all sensors and cameras");

    // Disable all sensors
    for (auto &controller : controllers_)
    {
      if (controller->IsEnabled())
      {
        controller->Disable();
      }
    }

    // Disable all cameras
    for (auto &camera : camera_controllers_)
    {
      if (camera->IsEnabled())
      {
        camera->Disable();
      }
    }

    // Shutdown ROS
    if (ros_.Initialized())
    {
      LOGI("Shutting down ROS");
      ros_.Shutdown();
    }
  }

  std::string GetSensorListJson()
  {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < controllers_.size(); ++i)
    {
      auto *c = controllers_[i].get();
      if (i > 0)
        ss << ",";
      ss << "{\"uniqueId\":\"" << c->UniqueId() << "\""
         << ",\"prettyName\":\"" << c->PrettyName() << "\""
         << ",\"sensorName\":\"" << c->SensorName() << "\""
         << ",\"vendor\":\"" << c->SensorVendor() << "\""
         << ",\"topicName\":\"" << c->TopicName() << "\""
         << ",\"topicType\":\"" << c->TopicType() << "\""
         << ",\"enabled\":" << (c->IsEnabled() ? "true" : "false")
         << ",\"type\":\"sensor\"}";
    }
    ss << "]";
    return ss.str();
  }

  std::string GetSensorDataJson(const std::string &unique_id)
  {
    for (auto &c : controllers_)
    {
      if (unique_id == c->UniqueId())
      {
        return c->GetLastMeasurementJson();
      }
    }
    for (auto &c : camera_controllers_)
    {
      if (unique_id == c->UniqueId())
      {
        return c->GetLastMeasurementJson();
      }
    }
    return "{}";
  }

  std::string GetCameraListJson()
  {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < camera_controllers_.size(); ++i)
    {
      auto *c = camera_controllers_[i].get();
      if (i > 0)
        ss << ",";
      auto [width, height] = c->GetResolution();
      ss << "{\"uniqueId\":\"" << c->UniqueId() << "\""
         << ",\"name\":\"" << c->GetCameraName() << "\""
         << ",\"enabled\":" << (c->IsEnabled() ? "true" : "false")
         << ",\"imageTopicName\":\"" << c->ImageTopicName() << "\""
         << ",\"imageTopicType\":\"" << c->ImageTopicType() << "\""
         << ",\"infoTopicName\":\"" << c->InfoTopicName() << "\""
         << ",\"infoTopicType\":\"" << c->InfoTopicType() << "\""
         << ",\"resolutionWidth\":" << width
         << ",\"resolutionHeight\":" << height
         << ",\"isFrontFacing\":" << (c->IsFrontFacing() ? "true" : "false")
         << ",\"sensorOrientation\":" << c->SensorOrientation()
         << "}";
    }
    ss << "]";
    return ss.str();
  }

  bool GetCameraFrameBytes(const std::string &unique_id,
                           std::vector<uint8_t> &out_data, int &out_width,
                           int &out_height)
  {
    for (auto &c : camera_controllers_)
    {
      if (unique_id == c->UniqueId())
      {
        return c->GetLastFrame(out_data, out_width, out_height);
      }
    }
    return false;
  }

  void EnableCamera(const std::string &unique_id)
  {
    for (auto &c : camera_controllers_)
    {
      if (unique_id == c->UniqueId())
      {
        c->EnableCamera();
        return;
      }
    }
  }

  void DisableCamera(const std::string &unique_id)
  {
    for (auto &c : camera_controllers_)
    {
      if (unique_id == c->UniqueId())
      {
        c->DisableCamera();
        return;
      }
    }
  }

  void EnableSensor(const std::string &unique_id)
  {
    for (auto &c : controllers_)
    {
      if (unique_id == c->UniqueId())
      {
        c->Enable();
        return;
      }
    }
  }

  void DisableSensor(const std::string &unique_id)
  {
    for (auto &c : controllers_)
    {
      if (unique_id == c->UniqueId())
      {
        c->Disable();
        return;
      }
    }
  }

  std::string GetDiscoveredTopicsJson()
  {
    if (!ros_.Initialized())
      return "[]";
    auto node = ros_.get_node();
    if (!node)
      return "[]";

    auto topics = node->get_topic_names_and_types();
    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto &[name, types] : topics)
    {
      if (!first)
        ss << ",";
      first = false;
      ss << "\"" << name << "\"";
    }
    ss << "]";
    return ss.str();
  }

  std::string cache_dir_;
  std::string package_name_;
  ros2_android::RosInterface ros_;
  ros2_android::Sensors sensors_;

  std::vector<std::unique_ptr<ros2_android::SensorDataProvider>>
      controllers_;
  std::vector<std::unique_ptr<ros2_android::CameraController>>
      camera_controllers_;

  ros2_android::CameraManager camera_manager_;
  std::unique_ptr<ros2_android::GpsLocationProvider> gps_provider_;
  bool started_cameras_ = false;
  std::vector<std::string> network_interfaces_;
};

static std::unique_ptr<AndroidApp> g_app;

// JNI function names: dots become underscores
// com.github.mowerick.ros2.android -> com_github_mowerick_ros2_android

extern "C"
{

  JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void * /*reserved*/)
  {
    g_jvm = vm;
    ros2_android::SetJavaVM(vm);
    return JNI_VERSION_1_6;
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeInit(
      JNIEnv *env, jobject /*thiz*/, jstring cache_dir, jstring package_name)
  {
    const char *cache_dir_c = env->GetStringUTFChars(cache_dir, nullptr);
    const char *package_name_c = env->GetStringUTFChars(package_name, nullptr);

    LOGI("nativeInit: cacheDir=%s packageName=%s", cache_dir_c, package_name_c);
    g_app = std::make_unique<AndroidApp>(
        std::string(cache_dir_c), std::string(package_name_c));

    env->ReleaseStringUTFChars(cache_dir, cache_dir_c);
    env->ReleaseStringUTFChars(package_name, package_name_c);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeDestroy(
      JNIEnv * /*env*/, jobject /*thiz*/)
  {
    if (g_app)
    {
      LOGI("nativeDestroy");
      g_app->sensors_.Shutdown();
      g_app->ros_.Shutdown();
      g_app.reset();
    }
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeSetNetworkInterfaces(
      JNIEnv *env, jobject /*thiz*/, jobjectArray interfaces)
  {
    if (!g_app)
      return;

    std::vector<std::string> ifaces;
    jsize len = env->GetArrayLength(interfaces);
    for (jsize i = 0; i < len; ++i)
    {
      jstring jstr = (jstring)env->GetObjectArrayElement(interfaces, i);
      const char *str = env->GetStringUTFChars(jstr, nullptr);
      ifaces.emplace_back(str);
      env->ReleaseStringUTFChars(jstr, str);
    }

    g_app->network_interfaces_ = std::move(ifaces);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeOnPermissionResult(
      JNIEnv *env, jobject /*thiz*/, jstring permission, jboolean granted)
  {
    if (!g_app)
      return;

    const char *perm = env->GetStringUTFChars(permission, nullptr);
    LOGI("Permission result: %s granted=%d", perm, (int)granted);

    if (std::string(perm) == "CAMERA" && granted)
    {
      g_app->StartCameras();
    }

    env->ReleaseStringUTFChars(permission, perm);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeStartRos(
      JNIEnv *env, jobject /*thiz*/, jint domain_id, jstring network_interface)
  {
    if (!g_app)
      return;

    const char *iface = env->GetStringUTFChars(network_interface, nullptr);
    g_app->StartRos(domain_id, std::string(iface));
    env->ReleaseStringUTFChars(network_interface, iface);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeStopRos(
      JNIEnv * /*env*/, jobject /*thiz*/)
  {
    if (!g_app)
      return;
    g_app->StopRos();
  }

  JNIEXPORT jstring JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeGetSensorList(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
      return env->NewStringUTF("[]");
    std::string json = g_app->GetSensorListJson();
    return env->NewStringUTF(json.c_str());
  }

  JNIEXPORT jstring JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeGetSensorData(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return env->NewStringUTF("{}");

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    std::string json = g_app->GetSensorDataJson(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
    return env->NewStringUTF(json.c_str());
  }

  JNIEXPORT jstring JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeGetCameraList(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
      return env->NewStringUTF("[]");
    std::string json = g_app->GetCameraListJson();
    return env->NewStringUTF(json.c_str());
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeEnableCamera(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->EnableCamera(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeDisableCamera(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->DisableCamera(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeEnableSensor(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->EnableSensor(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeDisableSensor(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->DisableSensor(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT jstring JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeGetNetworkInterfaces(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
      return env->NewStringUTF("[]");

    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < g_app->network_interfaces_.size(); ++i)
    {
      if (i > 0)
        ss << ",";
      ss << "\"" << g_app->network_interfaces_[i] << "\"";
    }
    ss << "]";
    return env->NewStringUTF(ss.str().c_str());
  }

  JNIEXPORT jstring JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeGetDiscoveredTopics(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
      return env->NewStringUTF("[]");
    std::string json = g_app->GetDiscoveredTopicsJson();
    return env->NewStringUTF(json.c_str());
  }

  JNIEXPORT jbyteArray JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeGetCameraFrame(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return nullptr;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    std::vector<uint8_t> data;
    int width = 0, height = 0;
    bool ok = g_app->GetCameraFrameBytes(std::string(id), data, width, height);
    env->ReleaseStringUTFChars(unique_id, id);

    if (!ok || data.empty())
      return nullptr;

    size_t total = 8 + data.size();
    jbyteArray result = env->NewByteArray(static_cast<jsize>(total));
    if (!result)
      return nullptr;

    // Prepend width and height as 2x int32 big-endian (8 bytes header)
    uint8_t header[8];
    header[0] = (width >> 24) & 0xFF;
    header[1] = (width >> 16) & 0xFF;
    header[2] = (width >> 8) & 0xFF;
    header[3] = width & 0xFF;
    header[4] = (height >> 24) & 0xFF;
    header[5] = (height >> 16) & 0xFF;
    header[6] = (height >> 8) & 0xFF;
    header[7] = height & 0xFF;

    env->SetByteArrayRegion(result, 0, 8,
                            reinterpret_cast<const jbyte *>(header));
    env->SetByteArrayRegion(result, 8, static_cast<jsize>(data.size()),
                            reinterpret_cast<const jbyte *>(data.data()));
    return result;
  }

  JNIEXPORT jstring JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeGetPendingNotifications(
      JNIEnv *env, jobject /*thiz*/)
  {
    std::vector<ros2_android::Notification> pending;
    ros2_android::NotificationQueue::Instance().Drain(pending);

    if (pending.empty())
    {
      return env->NewStringUTF("[]");
    }

    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < pending.size(); ++i)
    {
      if (i > 0)
        ss << ",";
      ss << "{\"severity\":\""
         << (pending[i].severity == ros2_android::NotificationSeverity::ERROR
                 ? "ERROR"
                 : "WARNING")
         << "\",\"message\":\"";
      // Escape quotes in message
      for (char c : pending[i].message)
      {
        if (c == '"')
          ss << "\\\"";
        else if (c == '\\')
          ss << "\\\\";
        else
          ss << c;
      }
      ss << "\"}";
    }
    ss << "]";
    return env->NewStringUTF(ss.str().c_str());
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeSetNotificationCallback(
      JNIEnv *env, jobject thiz)
  {
    // Clean up previous callback if it exists
    if (g_notification_callback_object != nullptr)
    {
      env->DeleteGlobalRef(g_notification_callback_object);
      g_notification_callback_object = nullptr;
      g_notification_callback_method = nullptr;
    }

    // Store global reference to the NativeBridge object
    g_notification_callback_object = env->NewGlobalRef(thiz);

    // Get the class and method ID for onNotification
    jclass clazz = env->GetObjectClass(thiz);
    g_notification_callback_method = env->GetStaticMethodID(
        clazz, "onNotification", "(Ljava/lang/String;Ljava/lang/String;)V");

    if (g_notification_callback_method == nullptr)
    {
      LOGE("Failed to find onNotification method");
      env->DeleteGlobalRef(g_notification_callback_object);
      g_notification_callback_object = nullptr;
      return;
    }

    env->DeleteLocalRef(clazz);

    // Set the callback in NotificationQueue
    ros2_android::NotificationQueue::Instance().SetCallback(
        [](ros2_android::NotificationSeverity severity, const std::string &message)
        {
          if (g_jvm == nullptr || g_notification_callback_object == nullptr ||
              g_notification_callback_method == nullptr)
          {
            return;
          }

          JNIEnv *env = nullptr;
          bool did_attach = false;
          int status = g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);

          if (status == JNI_EDETACHED)
          {
            if (g_jvm->AttachCurrentThread(&env, nullptr) != 0)
            {
              LOGE("Failed to attach thread for notification callback");
              return;
            }
            did_attach = true;
          }

          const char *severity_str = (severity == ros2_android::NotificationSeverity::ERROR) ? "ERROR" : "WARNING";
          jstring j_severity = env->NewStringUTF(severity_str);
          jstring j_message = env->NewStringUTF(message.c_str());

          jclass clazz = env->GetObjectClass(g_notification_callback_object);
          env->CallStaticVoidMethod(clazz, g_notification_callback_method, j_severity, j_message);

          env->DeleteLocalRef(j_severity);
          env->DeleteLocalRef(j_message);
          env->DeleteLocalRef(clazz);

          if (did_attach)
          {
            g_jvm->DetachCurrentThread();
          }
        });

    LOGI("Notification callback registered");
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_NativeBridge_nativeOnGpsLocation(
      JNIEnv * /*env*/, jobject /*thiz*/, jdouble latitude, jdouble longitude,
      jdouble altitude, jfloat accuracy, jfloat altitude_accuracy,
      jlong timestamp_ns)
  {
    if (!g_app || !g_app->gps_provider_)
    {
      return;
    }

    g_app->gps_provider_->OnLocationUpdate(latitude, longitude, altitude,
                                           accuracy, altitude_accuracy,
                                           timestamp_ns);
  }

} // extern "C"
