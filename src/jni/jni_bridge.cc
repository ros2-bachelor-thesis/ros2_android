#include <jni.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <turbojpeg.h>

#include "camera/base/camera_descriptor.h"
#include "camera/camera_manager.h"
#include "camera/controllers/camera_controller.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/notification_queue.h"
#include "core/sensor_data_callback_queue.h"
#include "core/camera_frame_callback_queue.h"
#include "core/debug_frame_callback_queue.h"
#include "jni/bitmap_utils.h"
#include "jni/jni_object_utils.h"
#include "jni/jvm.h"
#include "lidar/controllers/lidar_controller.h"
#include "lidar/impl/ydlidar_device.h"
#include "perception/controllers/perception_controller.h"
#include "ros/ros_interface.h"
#include <core/serial/serial.h>
#include "sensors/base/sensor_data_provider.h"
#include "sensors/controllers/gps_location_sensor_controller.h"
#include "sensors/impl/gps_location_sensor.h"
#include "sensors/sensor_manager.h"

static JavaVM *g_jvm = nullptr;

// Notification callback state
static std::mutex g_notification_callback_mutex;
static jobject g_notification_callback_object = nullptr;
static jmethodID g_notification_callback_method = nullptr;

// Sensor data callback state
static std::mutex g_sensor_data_callback_mutex;
static jobject g_sensor_data_callback_object = nullptr;
static jmethodID g_sensor_data_callback_method = nullptr;

// Camera frame callback state
static std::mutex g_camera_frame_callback_mutex;
static jobject g_camera_frame_callback_object = nullptr;
static jmethodID g_camera_frame_callback_method = nullptr;

// Debug frame callback state
static std::mutex g_debug_frame_callback_mutex;
static jobject g_debug_frame_callback_object = nullptr;
static jmethodID g_debug_frame_callback_method = nullptr;

// USB Serial JNI bridge state (used by android_jni_serial.cpp)
namespace ydlidar
{
  namespace core
  {
    namespace serial
    {
      JavaVM *g_javaVM = nullptr;
      jclass g_usbSerialBridgeClass = nullptr;
      jclass g_bufferedSerialClass = nullptr;
      jmethodID g_openDeviceMethod = nullptr;
      jmethodID g_closeDeviceMethod = nullptr;
      jmethodID g_availableMethod = nullptr;
      jmethodID g_readMethod = nullptr;
      jmethodID g_writeMethod = nullptr;
      jmethodID g_flushMethod = nullptr;
      // list_ports function stub (Android uses UsbSerialManager instead)
      std::vector<PortInfo> list_ports()
      {
        // Return empty - USB device discovery handled by UsbSerialManager in Java
        return std::vector<PortInfo>();
      }

    } // namespace serial
  } // namespace core
} // namespace ydlidar

class AndroidApp
{
public:
  AndroidApp(const std::string &cache_dir, const std::string &package_name)
      : cache_dir_(cache_dir),
        package_name_(package_name),
        sensor_manager_(package_name),
        network_manager_()
  {
    LOGI("Initializing SensorManager");
    sensor_manager_.Initialize();

    LOGI("Initializing GPS");
    gps_provider_ = std::make_unique<ros2_android::GpsLocationProvider>();
  }

  ~AndroidApp()
  {
    LOGI("AndroidApp destructor called");
    Cleanup();
  }

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
        // Create camera controller (publishes both raw and compressed)
        if (ros_)
        {
          auto camera_controller =
              std::make_unique<ros2_android::CameraController>(
                  &camera_manager_, cam_desc, *ros_);
          camera_controllers_.emplace_back(std::move(camera_controller));
        }
      }
    }
  }

  void StartRos(int32_t ros_domain_id, const std::string &network_interface, const std::string &device_id)
  {
    LOGI("Starting ROS with domain: %d, interface: %s, device: %s",
         ros_domain_id, network_interface.c_str(), device_id.c_str());

    // Check if already initialized - ignore if already running
    if (ros_ && ros_->Initialized())
    {
      LOGW("ROS is already initialized. Configuration not changed.");
      return;
    }

    // Generate Cyclone DDS config using NetworkManager (with caching)
    if (!network_manager_.GenerateCycloneDdsConfig(cache_dir_, ros_domain_id, network_interface))
    {
      LOGE("Failed to generate Cyclone DDS config");
      return;
    }

    // Create ROS interface with device_id
    ros_.emplace(device_id);
    LOGI("Initializing ROS with device_id: %s", device_id.c_str());
    ros_->Initialize(ros_domain_id);

    // Create sensor controllers using SensorManager
    size_t sensor_count = sensor_manager_.CreateControllers(*ros_);
    LOGI("SensorManager created %zu sensor controllers", sensor_count);

    // Create GPS controller
    auto gps_controller = std::make_unique<ros2_android::GpsController>(
        gps_provider_.get(), *ros_);
    gps_controllers_.emplace_back(std::move(gps_controller));
    LOGI("Created GPS controller");

    // Create camera controllers if cameras are available
    if (camera_manager_.HasCameras() && camera_controllers_.empty())
    {
      LOGI("Creating camera controllers");
      std::vector<ros2_android::CameraDescriptor> cameras =
          camera_manager_.GetCameras();
      for (auto cam_desc : cameras)
      {
        LOGI("Camera: %s", cam_desc.GetName().c_str());
        auto camera_controller =
            std::make_unique<ros2_android::CameraController>(
                &camera_manager_, cam_desc, *ros_);
        camera_controllers_.emplace_back(std::move(camera_controller));
      }
      started_cameras_ = true;
    }
  }

  void Cleanup()
  {
    LOGI("Cleaning up AndroidApp - START");

    // Disable and clear camera controllers first (they have threads)
    LOGI("Cleanup: Clearing %zu camera controllers", camera_controllers_.size());
    for (auto &camera : camera_controllers_)
    {
      if (camera->IsEnabled())
        camera->Disable();
    }
    camera_controllers_.clear();
    LOGI("Cleanup: Camera controllers cleared");

    // Clear GPS controllers
    LOGI("Cleanup: Clearing %zu GPS controllers", gps_controllers_.size());
    for (auto &gps : gps_controllers_)
    {
      if (gps->IsEnabled())
        gps->Disable();
    }
    gps_controllers_.clear();
    LOGI("Cleanup: GPS controllers cleared");

    // Clear LIDAR controllers
    LOGI("Cleanup: Clearing %zu LIDAR controllers", lidar_controllers_.size());
    for (auto &lidar : lidar_controllers_)
    {
      if (lidar->IsEnabled())
      {
        LOGI("Cleanup: Disabling LIDAR controller");
        lidar->Disable();
      }
    }
    lidar_controllers_.clear();
    LOGI("Cleanup: LIDAR controllers cleared");

    // Clear perception controller
    if (perception_controller_)
    {
      LOGI("Cleanup: Disabling perception controller");
      if (perception_controller_->IsEnabled())
      {
        perception_controller_->Disable();
      }
      perception_controller_.reset();
      LOGI("Cleanup: Perception controller cleared");
    }

    // IMPORTANT: Shutdown sensors BEFORE destructor to join threads cleanly
    // SensorManager destructor calls Shutdown(), but we need to do it NOW
    // to ensure sensor event loop threads finish before Cleanup() returns
    LOGI("Cleanup: Shutting down sensors (joining event loop threads)");
    sensor_manager_.Shutdown();
    LOGI("Cleanup: Sensor shutdown complete");

    // Clear sensor controllers (they're already shut down)
    sensor_manager_.ClearControllers();

    // Clear GPS provider
    gps_provider_.reset();

    // Shutdown ROS executor thread if running
    // IMPORTANT: Must wait for executor thread to finish BEFORE returning
    // to prevent thread-local cleanup crashes when app restarts
    if (ros_)
    {
      if (ros_->Initialized())
      {
        LOGI("Cleanup: Shutting down ROS context");
        // Context shutdown will stop the executor thread
        if (ros_->get_context() && ros_->get_context()->is_valid())
        {
          try
          {
            ros_->get_context()->shutdown("AndroidApp cleanup");
            LOGI("Cleanup: ROS context shutdown complete");
          }
          catch (const std::exception &e)
          {
            LOGE("Cleanup: Exception during ROS context shutdown: %s", e.what());
          }
        }
      }

      LOGI("Cleanup: Resetting ROS interface (will join executor thread)");
      ros_.reset(); // Destructor will join executor_thread_
      LOGI("Cleanup: ROS interface destroyed");
    }

    LOGI("Cleaning up AndroidApp - COMPLETE");
  }

  // JSON methods removed - using structured data instead (GetSensorList, GetCameraList, GetSensorData)

  // Structured data getters (replacing JSON)
  std::vector<ros2_android::jni::SensorInfoData> GetSensorList()
  {
    std::vector<ros2_android::jni::SensorInfoData> result;

    // Get sensor controllers from SensorManager
    for (const auto &c : sensor_manager_.GetControllers())
    {
      ros2_android::jni::SensorInfoData data;
      data.uniqueId = c->UniqueId();
      data.prettyName = c->PrettyName();
      data.sensorName = c->SensorName();
      data.vendor = c->SensorVendor();
      data.topicName = c->TopicName();
      data.topicType = c->TopicType();
      data.enabled = c->IsEnabled();
      result.push_back(data);
    }

    // Add GPS controller
    for (const auto &gps : gps_controllers_)
    {
      ros2_android::jni::SensorInfoData data;
      data.uniqueId = gps->UniqueId();
      data.prettyName = gps->PrettyName();
      data.sensorName = gps->SensorName();
      data.vendor = gps->SensorVendor();
      data.topicName = gps->TopicName();
      data.topicType = gps->TopicType();
      data.enabled = gps->IsEnabled();
      result.push_back(data);
    }

    return result;
  }

  std::vector<ros2_android::jni::CameraInfoData> GetCameraList()
  {
    std::vector<ros2_android::jni::CameraInfoData> result;
    for (const auto &c : camera_controllers_)
    {
      ros2_android::jni::CameraInfoData data;
      auto [width, height] = c->GetResolution();
      data.uniqueId = c->UniqueId();
      data.name = c->GetCameraName();
      data.enabled = c->IsEnabled();
      data.imageTopicName = c->ImageTopicName();
      data.imageTopicType = c->ImageTopicType();
      data.compressedImageTopicName = c->CompressedImageTopicName();
      data.compressedImageTopicType = c->CompressedImageTopicType();
      data.infoTopicName = c->InfoTopicName();
      data.infoTopicType = c->InfoTopicType();
      data.resolutionWidth = width;
      data.resolutionHeight = height;
      data.isFrontFacing = c->IsFrontFacing();
      data.sensorOrientation = c->SensorOrientation();
      result.push_back(data);
    }
    return result;
  }

  bool GetSensorData(const std::string &unique_id, ros2_android::jni::SensorReadingData &out_data)
  {
    // Check sensor managers controllers
    for (const auto &c : sensor_manager_.GetControllers())
    {
      if (unique_id == c->UniqueId())
      {
        return c->GetLastMeasurement(out_data);
      }
    }

    // Check GPS controllers
    for (const auto &gps : gps_controllers_)
    {
      if (unique_id == gps->UniqueId())
      {
        return gps->GetLastMeasurement(out_data);
      }
    }

    return false;
  }

  std::vector<std::string> GetDiscoveredTopics()
  {
    std::vector<std::string> result;
    if (!ros_ || !ros_->Initialized())
      return result;
    auto node = ros_->get_node();
    if (!node)
      return result;

    auto topics = node->get_topic_names_and_types();
    for (const auto &[name, types] : topics)
    {
      result.push_back(name);
    }
    return result;
  }

  bool GetCameraFrameBytes(const std::string &unique_id,
                           std::vector<uint8_t> &out_data, int &out_width,
                           int &out_height)
  {
    for (auto &c : camera_controllers_)
    {
      const auto *ptr = c.get();
      if (unique_id == ptr->UniqueId())
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
      const auto *ptr = c.get();
      if (unique_id == ptr->UniqueId())
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
      const auto *ptr = c.get();
      if (unique_id == ptr->UniqueId())
      {
        c->DisableCamera();
        return;
      }
    }
  }

  void EnableSensor(const std::string &unique_id)
  {
    // Try sensor manager first
    if (sensor_manager_.EnableSensor(unique_id))
      return;

    // Try GPS controllers
    for (auto &gps : gps_controllers_)
    {
      if (unique_id == gps->UniqueId())
      {
        gps->Enable();
        return;
      }
    }
  }

  void DisableSensor(const std::string &unique_id)
  {
    // Try sensor manager first
    if (sensor_manager_.DisableSensor(unique_id))
      return;

    // Try GPS controllers
    for (auto &gps : gps_controllers_)
    {
      if (unique_id == gps->UniqueId())
      {
        gps->Disable();
        return;
      }
    }
  }

  // LIDAR device management

  bool ConnectLidar(const std::string &usb_path, const std::string &unique_id, int baudrate)
  {
    LOGI("ConnectLidar: usb=%s, id=%s, baudrate=%d", usb_path.c_str(), unique_id.c_str(), baudrate);

    // Check if already connected
    for (const auto &controller : lidar_controllers_)
    {
      if (controller->GetUniqueId() == unique_id)
      {
        LOGW("LIDAR %s already connected", unique_id.c_str());
        return false;
      }
    }

    // Create YDLidar device with USB path and baudrate
    auto device = std::make_unique<ros2_android::YDLidarDevice>(usb_path, unique_id, baudrate);

    // Create controller (dereference optional ros_)
    auto controller = std::make_unique<ros2_android::LidarController>(std::move(device), *ros_);

    lidar_controllers_.push_back(std::move(controller));
    LOGI("LIDAR connected successfully: %s", unique_id.c_str());

    return true;
  }

  bool DisconnectLidar(const std::string &unique_id)
  {
    LOGI("DisconnectLidar: id=%s", unique_id.c_str());

    auto it = std::find_if(lidar_controllers_.begin(), lidar_controllers_.end(),
                           [&](const std::unique_ptr<ros2_android::LidarController> &controller)
                           {
                             return controller->GetUniqueId() == unique_id;
                           });

    if (it != lidar_controllers_.end())
    {
      // Disable before removing
      (*it)->Disable();
      lidar_controllers_.erase(it);
      LOGI("LIDAR disconnected: %s", unique_id.c_str());
      return true;
    }

    LOGW("LIDAR not found: %s", unique_id.c_str());
    return false;
  }

  bool EnableLidar(const std::string &unique_id)
  {
    LOGI("EnableLidar: id=%s", unique_id.c_str());

    for (auto &controller : lidar_controllers_)
    {
      if (controller->GetUniqueId() == unique_id)
      {
        controller->Enable();
        LOGI("LIDAR publishing enabled: %s", unique_id.c_str());
        return true;
      }
    }

    LOGW("LIDAR not found: %s", unique_id.c_str());
    return false;
  }

  bool DisableLidar(const std::string &unique_id)
  {
    LOGI("DisableLidar: id=%s", unique_id.c_str());

    for (auto &controller : lidar_controllers_)
    {
      if (controller->GetUniqueId() == unique_id)
      {
        controller->Disable();
        LOGI("LIDAR publishing disabled: %s", unique_id.c_str());
        return true;
      }
    }

    LOGW("LIDAR not found: %s", unique_id.c_str());
    return false;
  }

  // GetDiscoveredTopicsJson removed - using structured GetDiscoveredTopics() instead

  std::string cache_dir_;
  std::string package_name_;
  std::optional<ros2_android::RosInterface> ros_;

  // Managers
  ros2_android::SensorManager sensor_manager_;
  ros2_android::NetworkManager network_manager_;
  ros2_android::CameraManager camera_manager_;

  // GPS
  std::vector<std::unique_ptr<ros2_android::SensorDataProvider>>
      gps_controllers_;
  std::unique_ptr<ros2_android::GpsLocationProvider> gps_provider_;

  // Cameras
  std::vector<std::unique_ptr<ros2_android::CameraController>>
      camera_controllers_;
  bool started_cameras_ = false;

  // LIDAR devices
  std::vector<std::unique_ptr<ros2_android::LidarController>> lidar_controllers_;

  // Perception (ML pipeline)
  std::unique_ptr<ros2_android::PerceptionController> perception_controller_;
};

static std::unique_ptr<AndroidApp> g_app;

// JNI function names: dots become underscores
// com.github.mowerick.ros2.android -> com_github_mowerick_ros2_android

extern "C"
{

  JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void * /*reserved*/)
  {
    g_jvm = vm;
    ydlidar::core::serial::g_javaVM = vm; // For USB Serial JNI bridge
    ros2_android::SetJavaVM(vm);
    return JNI_VERSION_1_6;
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeInit(
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
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeDestroy(
      JNIEnv * /*env*/, jobject /*thiz*/)
  {
    LOGI("nativeDestroy called");
    if (g_app)
    {
      LOGI("nativeDestroy: g_app exists, calling Cleanup");
      g_app->Cleanup();
      LOGI("nativeDestroy: Cleanup done, resetting g_app");
      g_app.reset();
      LOGI("nativeDestroy: g_app reset complete");
    }
    else
    {
      LOGI("nativeDestroy: g_app was already null");
    }
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeSetNetworkInterfaces(
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

    g_app->network_manager_.SetNetworkInterfaces(std::move(ifaces));
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeOnPermissionResult(
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
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeStartRos(
      JNIEnv *env, jobject /*thiz*/, jint domain_id, jstring network_interface, jstring device_id)
  {
    if (!g_app)
      return;

    const char *iface = env->GetStringUTFChars(network_interface, nullptr);
    const char *dev_id = env->GetStringUTFChars(device_id, nullptr);
    g_app->StartRos(domain_id, std::string(iface), std::string(dev_id));
    env->ReleaseStringUTFChars(network_interface, iface);
    env->ReleaseStringUTFChars(device_id, dev_id);
  }

  JNIEXPORT jobjectArray JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetSensorList(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
    {
      return ros2_android::jni::CreateSensorInfoArray(env, {});
    }
    auto sensors = g_app->GetSensorList();
    return ros2_android::jni::CreateSensorInfoArray(env, sensors);
  }

  JNIEXPORT jobject JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetSensorData(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return nullptr;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    ros2_android::jni::SensorReadingData data;
    bool success = g_app->GetSensorData(std::string(id), data);
    env->ReleaseStringUTFChars(unique_id, id);

    if (!success)
      return nullptr;

    return ros2_android::jni::CreateSensorReading(env, data);
  }

  JNIEXPORT jobjectArray JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetCameraList(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
    {
      return ros2_android::jni::CreateCameraInfoArray(env, {});
    }
    auto cameras = g_app->GetCameraList();
    return ros2_android::jni::CreateCameraInfoArray(env, cameras);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeEnableCamera(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->EnableCamera(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeDisableCamera(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->DisableCamera(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeEnableSensor(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->EnableSensor(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeDisableSensor(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    g_app->DisableSensor(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);
  }

  JNIEXPORT jobjectArray JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetNetworkInterfaces(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
    {
      return ros2_android::jni::CreateStringArray(env, {});
    }
    return ros2_android::jni::CreateStringArray(env, g_app->network_manager_.GetNetworkInterfaces());
  }

  JNIEXPORT jobjectArray JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetDiscoveredTopics(
      JNIEnv *env, jobject /*thiz*/)
  {
    if (!g_app)
    {
      return ros2_android::jni::CreateStringArray(env, {});
    }
    auto topics = g_app->GetDiscoveredTopics();
    return ros2_android::jni::CreateStringArray(env, topics);
  }

  JNIEXPORT jobject JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetCameraFrame(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return nullptr;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    std::vector<uint8_t> rgba_data;
    int width = 0, height = 0;
    bool ok = g_app->GetCameraFrameBytes(std::string(id), rgba_data, width, height);
    env->ReleaseStringUTFChars(unique_id, id);

    if (!ok || rgba_data.empty())
      return nullptr;

    // Create bitmap from RGBA data
    return ros2_android::jni::CreateBitmapFromRGB(env, rgba_data.data(), width, height);
  }

  JNIEXPORT jstring JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetPendingNotifications(
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
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeSetNotificationCallback(
      JNIEnv *env, jobject thiz)
  {
    std::lock_guard<std::mutex> lock(g_notification_callback_mutex);

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
          std::lock_guard<std::mutex> lock(g_notification_callback_mutex);
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
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeOnGpsLocation(
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

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeSetSensorDataCallback(
      JNIEnv *env, jobject thiz)
  {
    std::lock_guard<std::mutex> lock(g_sensor_data_callback_mutex);

    // Clean up previous callback if it exists
    if (g_sensor_data_callback_object != nullptr)
    {
      env->DeleteGlobalRef(g_sensor_data_callback_object);
      g_sensor_data_callback_object = nullptr;
      g_sensor_data_callback_method = nullptr;
    }

    // Store global reference to the NativeBridge object
    g_sensor_data_callback_object = env->NewGlobalRef(thiz);

    // Get the class and method ID for onSensorDataUpdate
    jclass clazz = env->GetObjectClass(thiz);
    g_sensor_data_callback_method = env->GetStaticMethodID(
        clazz, "onSensorDataUpdate", "(Ljava/lang/String;)V");

    if (g_sensor_data_callback_method == nullptr)
    {
      LOGE("Failed to find onSensorDataUpdate method");
      env->DeleteGlobalRef(g_sensor_data_callback_object);
      g_sensor_data_callback_object = nullptr;
      return;
    }

    env->DeleteLocalRef(clazz);

    // Set the callback in SensorDataCallbackQueue
    ros2_android::SensorDataCallbackQueue::Instance().SetCallback(
        [](const std::string &sensor_id)
        {
          std::lock_guard<std::mutex> lock(g_sensor_data_callback_mutex);
          if (g_jvm == nullptr || g_sensor_data_callback_object == nullptr ||
              g_sensor_data_callback_method == nullptr)
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
              LOGE("Failed to attach thread for sensor data callback");
              return;
            }
            did_attach = true;
          }

          jstring j_sensor_id = env->NewStringUTF(sensor_id.c_str());
          jclass clazz = env->GetObjectClass(g_sensor_data_callback_object);
          env->CallStaticVoidMethod(clazz, g_sensor_data_callback_method, j_sensor_id);

          env->DeleteLocalRef(j_sensor_id);
          env->DeleteLocalRef(clazz);

          if (did_attach)
          {
            g_jvm->DetachCurrentThread();
          }
        });

    LOGI("Sensor data callback registered");
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeSetCameraFrameCallback(
      JNIEnv *env, jobject thiz)
  {
    std::lock_guard<std::mutex> lock(g_camera_frame_callback_mutex);

    // Clean up previous callback if it exists
    if (g_camera_frame_callback_object != nullptr)
    {
      env->DeleteGlobalRef(g_camera_frame_callback_object);
      g_camera_frame_callback_object = nullptr;
      g_camera_frame_callback_method = nullptr;
    }

    // Store global reference to the NativeBridge object
    g_camera_frame_callback_object = env->NewGlobalRef(thiz);

    // Get the class and method ID for onCameraFrameUpdate
    jclass clazz = env->GetObjectClass(thiz);
    g_camera_frame_callback_method = env->GetStaticMethodID(
        clazz, "onCameraFrameUpdate", "(Ljava/lang/String;)V");

    if (g_camera_frame_callback_method == nullptr)
    {
      LOGE("Failed to find onCameraFrameUpdate method");
      env->DeleteGlobalRef(g_camera_frame_callback_object);
      g_camera_frame_callback_object = nullptr;
      return;
    }

    env->DeleteLocalRef(clazz);

    // Set the callback in CameraFrameCallbackQueue
    ros2_android::CameraFrameCallbackQueue::Instance().SetCallback(
        [](const std::string &camera_id)
        {
          std::lock_guard<std::mutex> lock(g_camera_frame_callback_mutex);
          if (g_jvm == nullptr || g_camera_frame_callback_object == nullptr ||
              g_camera_frame_callback_method == nullptr)
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
              LOGE("Failed to attach thread for camera frame callback");
              return;
            }
            did_attach = true;
          }

          jstring j_camera_id = env->NewStringUTF(camera_id.c_str());
          jclass clazz = env->GetObjectClass(g_camera_frame_callback_object);
          env->CallStaticVoidMethod(clazz, g_camera_frame_callback_method, j_camera_id);

          env->DeleteLocalRef(j_camera_id);
          env->DeleteLocalRef(clazz);

          if (did_attach)
          {
            g_jvm->DetachCurrentThread();
          }
        });

    LOGI("Camera frame callback registered");
  }

  // LIDAR device management
  JNIEXPORT jboolean JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeConnectLidar(
      JNIEnv *env, jobject /*thiz*/, jstring usb_path, jstring unique_id, jint baudrate)
  {
    if (!g_app)
      return JNI_FALSE;

    const char *path = env->GetStringUTFChars(usb_path, nullptr);
    const char *id = env->GetStringUTFChars(unique_id, nullptr);

    bool success = g_app->ConnectLidar(std::string(path), std::string(id), static_cast<int>(baudrate));

    env->ReleaseStringUTFChars(usb_path, path);
    env->ReleaseStringUTFChars(unique_id, id);

    return success ? JNI_TRUE : JNI_FALSE;
  }

  JNIEXPORT jboolean JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeDisconnectLidar(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return JNI_FALSE;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    bool success = g_app->DisconnectLidar(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);

    return success ? JNI_TRUE : JNI_FALSE;
  }

  JNIEXPORT jobjectArray JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetLidarList(
      JNIEnv *env, jobject /*thiz*/)
  {
    LOGI("nativeGetLidarList called");

    if (!g_app)
    {
      LOGI("nativeGetLidarList: g_app is null, returning empty array");
      return ros2_android::jni::CreateExternalDeviceInfoArray(env, {});
    }

    LOGI("nativeGetLidarList: g_app exists, lidar_controllers size: %zu", g_app->lidar_controllers_.size());

    // Convert lidar controllers to ExternalDeviceInfoData
    std::vector<ros2_android::jni::ExternalDeviceInfoData> devices;
    for (size_t i = 0; i < g_app->lidar_controllers_.size(); ++i)
    {
      LOGI("nativeGetLidarList: Processing controller %zu", i);
      const auto &controller = g_app->lidar_controllers_[i];

      if (!controller)
      {
        LOGE("nativeGetLidarList: Controller %zu is null! Skipping...", i);
        continue;
      }

      LOGI("nativeGetLidarList: Controller %zu is valid, getting data...", i);
      ros2_android::jni::ExternalDeviceInfoData data;
      data.uniqueId = controller->GetUniqueId();
      LOGI("nativeGetLidarList: Got uniqueId: %s", data.uniqueId.c_str());
      data.name = "YDLIDAR";
      data.deviceType = "LIDAR";
      data.usbPath = controller->GetDevicePath();
      LOGI("nativeGetLidarList: Got device path: %s", data.usbPath.c_str());
      data.vendorId = 0;  // Not tracked
      data.productId = 0; // Not tracked
      data.topicName = controller->TopicName();
      data.topicType = controller->TopicType();
      data.connected = true; // If it's in the list, it's connected
      data.enabled = controller->IsEnabled();
      devices.push_back(data);
      LOGI("nativeGetLidarList: Controller %zu processed successfully", i);
    }

    LOGI("nativeGetLidarList: All controllers processed, returning %zu devices", devices.size());

    return ros2_android::jni::CreateExternalDeviceInfoArray(env, devices);
  }

  JNIEXPORT jboolean JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeEnableLidar(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return JNI_FALSE;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    bool success = g_app->EnableLidar(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);

    return success ? JNI_TRUE : JNI_FALSE;
  }

  JNIEXPORT jboolean JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeDisableLidar(
      JNIEnv *env, jobject /*thiz*/, jstring unique_id)
  {
    if (!g_app)
      return JNI_FALSE;

    const char *id = env->GetStringUTFChars(unique_id, nullptr);
    bool success = g_app->DisableLidar(std::string(id));
    env->ReleaseStringUTFChars(unique_id, id);

    return success ? JNI_TRUE : JNI_FALSE;
  }

  // Initialize USB Serial JNI bridge (cache Java class and method IDs)
  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_UsbSerialBridge_nativeInitJNI(
      JNIEnv *env, jclass /*clazz*/)
  {
    using namespace ydlidar::core::serial;
    LOGI("Initializing USB Serial JNI bridge");

    // Find UsbSerialBridge class
    jclass localBridgeClass = env->FindClass("com/github/mowerick/ros2/android/util/UsbSerialBridge");
    if (localBridgeClass == nullptr)
    {
      LOGE("Failed to find UsbSerialBridge class");
      return;
    }
    g_usbSerialBridgeClass = reinterpret_cast<jclass>(env->NewGlobalRef(localBridgeClass));
    env->DeleteLocalRef(localBridgeClass);

    // Find BufferedUsbSerialPort class
    jclass localPortClass = env->FindClass("com/github/mowerick/ros2/android/util/BufferedUsbSerialPort");
    if (localPortClass == nullptr)
    {
      LOGE("Failed to find BufferedUsbSerialPort class");
      return;
    }
    g_bufferedSerialClass = reinterpret_cast<jclass>(env->NewGlobalRef(localPortClass));
    env->DeleteLocalRef(localPortClass);

    // Cache method IDs for UsbSerialBridge
    g_openDeviceMethod = env->GetStaticMethodID(
        g_usbSerialBridgeClass,
        "openDevice",
        "(Ljava/lang/String;IIII)Lcom/github/mowerick/ros2/android/util/BufferedUsbSerialPort;");
    if (g_openDeviceMethod == nullptr)
    {
      LOGE("Failed to find UsbSerialBridge.openDevice method");
      return;
    }

    g_closeDeviceMethod = env->GetStaticMethodID(
        g_usbSerialBridgeClass,
        "closeDevice",
        "(Ljava/lang/String;)V");
    if (g_closeDeviceMethod == nullptr)
    {
      LOGE("Failed to find UsbSerialBridge.closeDevice method");
      return;
    }

    // Cache method IDs for BufferedUsbSerialPort
    g_availableMethod = env->GetMethodID(g_bufferedSerialClass, "available", "()I");
    if (g_availableMethod == nullptr)
    {
      LOGE("Failed to find BufferedUsbSerialPort.available method");
      return;
    }

    g_readMethod = env->GetMethodID(g_bufferedSerialClass, "read", "([BI)I");
    if (g_readMethod == nullptr)
    {
      LOGE("Failed to find BufferedUsbSerialPort.read method");
      return;
    }

    g_writeMethod = env->GetMethodID(g_bufferedSerialClass, "write", "([BI)V");
    if (g_writeMethod == nullptr)
    {
      LOGE("Failed to find BufferedUsbSerialPort.write method");
      return;
    }

    g_flushMethod = env->GetMethodID(g_bufferedSerialClass, "flush", "(ZZ)V");
    if (g_flushMethod == nullptr)
    {
      LOGE("Failed to find BufferedUsbSerialPort.flush method");
      return;
    }

    LOGI("USB Serial JNI bridge initialized successfully");
  }

  // ============================================================================
  // Perception (Object Detection) JNI Functions
  // ============================================================================

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_enablePerception(
      JNIEnv *env, jclass /*clazz*/, jstring models_path)
  {
    if (!g_app)
    {
      LOGE("enablePerception: g_app is null");
      return;
    }

    const char *models_path_c = env->GetStringUTFChars(models_path, nullptr);
    LOGI("enablePerception: models_path=%s", models_path_c);

    if (!g_app->perception_controller_)
    {
      // Create perception controller if it doesn't exist
      g_app->perception_controller_ = std::make_unique<ros2_android::PerceptionController>(
          g_app->ros_.value(), std::string(models_path_c));

      if (!g_app->perception_controller_->IsReady())
      {
        LOGE("enablePerception: Failed to initialize perception controller");
        g_app->perception_controller_.reset();
        env->ReleaseStringUTFChars(models_path, models_path_c);
        return;
      }
    }

    // Enable perception
    g_app->perception_controller_->Enable();
    LOGI("enablePerception: Perception enabled");

    env->ReleaseStringUTFChars(models_path, models_path_c);
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_disablePerception(
      JNIEnv * /*env*/, jclass /*clazz*/)
  {
    if (!g_app)
    {
      LOGE("disablePerception: g_app is null");
      return;
    }

    if (g_app->perception_controller_)
    {
      LOGI("disablePerception: Disabling perception");
      g_app->perception_controller_->Disable();
    }
    else
    {
      LOGW("disablePerception: perception_controller_ is null");
    }
  }

  JNIEXPORT jboolean JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_isPerceptionEnabled(
      JNIEnv * /*env*/, jclass /*clazz*/)
  {
    if (!g_app || !g_app->perception_controller_)
    {
      return JNI_FALSE;
    }

    return g_app->perception_controller_->IsEnabled() ? JNI_TRUE : JNI_FALSE;
  }

  // ============================================================================
  // Perception Debug Visualization JNI Functions
  // ============================================================================

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeEnablePerceptionVisualization(
      JNIEnv * /*env*/, jclass /*clazz*/, jboolean enable)
  {
    if (!g_app || !g_app->perception_controller_)
    {
      LOGW("nativeEnablePerceptionVisualization: perception_controller not available");
      return;
    }

    g_app->perception_controller_->EnableVisualization(enable == JNI_TRUE);
    LOGI("Perception visualization %s", enable ? "enabled" : "disabled");
  }

  JNIEXPORT void JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeSetDebugFrameCallback(
      JNIEnv *env, jobject thiz)
  {
    std::lock_guard<std::mutex> lock(g_debug_frame_callback_mutex);

    // Clean up previous callback if it exists
    if (g_debug_frame_callback_object != nullptr)
    {
      env->DeleteGlobalRef(g_debug_frame_callback_object);
      g_debug_frame_callback_object = nullptr;
      g_debug_frame_callback_method = nullptr;
    }

    // Store global reference to the NativeBridge object
    g_debug_frame_callback_object = env->NewGlobalRef(thiz);

    // Get the class and method ID for onDebugFrameUpdate
    jclass clazz = env->GetObjectClass(thiz);
    g_debug_frame_callback_method = env->GetStaticMethodID(
        clazz, "onDebugFrameUpdate", "(Ljava/lang/String;)V");

    if (g_debug_frame_callback_method == nullptr)
    {
      LOGE("Failed to find onDebugFrameUpdate method");
      env->DeleteGlobalRef(g_debug_frame_callback_object);
      g_debug_frame_callback_object = nullptr;
      return;
    }

    env->DeleteLocalRef(clazz);

    // Set the callback in DebugFrameCallbackQueue
    ros2_android::DebugFrameCallbackQueue::Instance().SetCallback(
        [](const std::string &frame_id)
        {
          std::lock_guard<std::mutex> lock(g_debug_frame_callback_mutex);
          if (g_jvm == nullptr || g_debug_frame_callback_object == nullptr ||
              g_debug_frame_callback_method == nullptr)
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
              LOGE("Failed to attach thread for debug frame callback");
              return;
            }
            did_attach = true;
          }

          jstring j_frame_id = env->NewStringUTF(frame_id.c_str());
          jclass clazz = env->GetObjectClass(g_debug_frame_callback_object);
          env->CallStaticVoidMethod(clazz, g_debug_frame_callback_method, j_frame_id);

          env->DeleteLocalRef(j_frame_id);
          env->DeleteLocalRef(clazz);

          if (did_attach)
          {
            g_jvm->DetachCurrentThread();
          }
        });

    LOGI("Debug frame callback registered");
  }

  JNIEXPORT jobject JNICALL
  Java_com_github_mowerick_ros2_android_util_NativeBridge_nativeGetDebugFrame(
      JNIEnv *env, jclass /*clazz*/, jstring frame_id)
  {
    if (!g_app || !g_app->perception_controller_)
    {
      return nullptr;
    }

    const char *frame_id_c = env->GetStringUTFChars(frame_id, nullptr);
    std::string frame_id_str(frame_id_c);
    env->ReleaseStringUTFChars(frame_id, frame_id_c);

    // Get JPEG data from perception controller
    std::vector<uint8_t> jpeg_data;
    if (!g_app->perception_controller_->GetDebugFrame(frame_id_str, jpeg_data))
    {
      return nullptr;
    }

    if (jpeg_data.empty())
    {
      return nullptr;
    }

    // Decompress JPEG to create Android Bitmap
    // Use TurboJPEG to decode
    tjhandle decompressor = tjInitDecompress();
    if (!decompressor)
    {
      LOGE("Failed to init TurboJPEG for debug frame decode");
      return nullptr;
    }

    int width, height, jpegSubsamp, jpegColorspace;
    int tj_result = tjDecompressHeader3(
        decompressor,
        jpeg_data.data(),
        jpeg_data.size(),
        &width,
        &height,
        &jpegSubsamp,
        &jpegColorspace);

    if (tj_result != 0)
    {
      LOGE("Failed to decode debug frame JPEG header");
      tjDestroy(decompressor);
      return nullptr;
    }

    // Allocate RGBA buffer for Android bitmap
    std::vector<uint8_t> rgba_buffer(width * height * 4);

    tj_result = tjDecompress2(
        decompressor,
        jpeg_data.data(),
        jpeg_data.size(),
        rgba_buffer.data(),
        width,
        0,  // pitch
        height,
        TJPF_RGBA,  // Android expects RGBA
        TJFLAG_FASTDCT);

    tjDestroy(decompressor);

    if (tj_result != 0)
    {
      LOGE("Failed to decompress debug frame JPEG");
      return nullptr;
    }

    // Create Android Bitmap from RGBA data
    return ros2_android::jni::CreateBitmapFromRGB(env, rgba_buffer.data(), width, height);
  }

} // extern "C"
