#include <jni.h>
#include <android/native_window_jni.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "camera_descriptor.h"
#include "camera_manager.h"
#include "controller.h"
#include "controllers/accelerometer_sensor_controller.h"
#include "controllers/barometer_sensor_controller.h"
#include "controllers/camera_controller.h"
#include "controllers/gyroscope_sensor_controller.h"
#include "controllers/illuminance_sensor_controller.h"
#include "controllers/list_controller.h"
#include "controllers/magnetometer_sensor_controller.h"
#include "controllers/ros_domain_id_controller.h"
#include "events.h"
#include "gui.h"
#include "jvm.h"
#include "log.h"
#include "ros_interface.h"
#include "sensors.h"

static JavaVM* g_jvm = nullptr;

class AndroidApp {
 public:
  AndroidApp(const std::string& cache_dir, const std::string& package_name)
      : cache_dir_(cache_dir),
        package_name_(package_name),
        sensors_(package_name),
        ros_domain_id_controller_() {
    ros_domain_id_controller_.SetListener(std::bind(
        &AndroidApp::OnRosDomainIdChanged, this, std::placeholders::_1));
    PushController(&ros_domain_id_controller_);

    list_controller_.SetListener(
        std::bind(&AndroidApp::OnNavigateBack, this, std::placeholders::_1));
    list_controller_.SetListener(
        std::bind(&AndroidApp::OnNavigateTo, this, std::placeholders::_1));

    LOGI("Initalizing Sensors");
    sensors_.Initialize();

    for (auto& sensor : sensors_.GetSensors()) {
      if (ASENSOR_TYPE_LIGHT == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::IlluminanceSensorController>(
                static_cast<sensors_for_ros::IlluminanceSensor*>(sensor.get()),
                ros_);
        controller->SetListener(std::bind(&AndroidApp::OnNavigateBack, this,
                                          std::placeholders::_1));
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_GYROSCOPE == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::GyroscopeSensorController>(
                static_cast<sensors_for_ros::GyroscopeSensor*>(sensor.get()),
                ros_);
        controller->SetListener(std::bind(&AndroidApp::OnNavigateBack, this,
                                          std::placeholders::_1));
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_ACCELEROMETER == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::AccelerometerSensorController>(
                static_cast<sensors_for_ros::AccelerometerSensor*>(
                    sensor.get()),
                ros_);
        controller->SetListener(std::bind(&AndroidApp::OnNavigateBack, this,
                                          std::placeholders::_1));
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_PRESSURE == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::BarometerSensorController>(
                static_cast<sensors_for_ros::BarometerSensor*>(sensor.get()),
                ros_);
        controller->SetListener(std::bind(&AndroidApp::OnNavigateBack, this,
                                          std::placeholders::_1));
        controllers_.emplace_back(std::move(controller));
      } else if (ASENSOR_TYPE_MAGNETIC_FIELD == sensor->Descriptor().type) {
        auto controller =
            std::make_unique<sensors_for_ros::MagnetometerSensorController>(
                static_cast<sensors_for_ros::MagnetometerSensor*>(sensor.get()),
                ros_);
        controller->SetListener(std::bind(&AndroidApp::OnNavigateBack, this,
                                          std::placeholders::_1));
        controllers_.emplace_back(std::move(controller));
      }
    }

    for (const auto& controller : controllers_) {
      list_controller_.AddController(controller.get());
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
        std::unique_ptr<sensors_for_ros::CameraController> camera_controller(
            new sensors_for_ros::CameraController(&camera_manager_, cam_desc,
                                                  ros_));
        camera_controller->SetListener(std::bind(&AndroidApp::OnNavigateBack,
                                                 this, std::placeholders::_1));
        controllers_.emplace_back(std::move(camera_controller));
        list_controller_.AddController(controllers_.back().get());
      }
    }
  }

  std::string cache_dir_;
  std::string package_name_;
  sensors_for_ros::RosInterface ros_;
  sensors_for_ros::Sensors sensors_;
  sensors_for_ros::GUI gui_;

  sensors_for_ros::RosDomainIdController ros_domain_id_controller_;
  sensors_for_ros::ListController list_controller_;
  std::vector<std::unique_ptr<sensors_for_ros::Controller>> controllers_;
  std::vector<sensors_for_ros::Controller*> controller_stack_;

  sensors_for_ros::CameraManager camera_manager_;
  bool started_cameras_ = false;

 private:
  void PushController(sensors_for_ros::Controller* controller) {
    if (controller) {
      controller_stack_.push_back(controller);
      gui_.SetController(controller);
    }
  }

  void PopController() {
    if (controller_stack_.size() > 1) {
      controller_stack_.pop_back();
      gui_.SetController(controller_stack_.back());
    }
  }

  void OnNavigateBack(const sensors_for_ros::event::GuiNavigateBack&) {
    LOGI("Poping controller!");
    PopController();
  }

  void OnNavigateTo(const sensors_for_ros::event::GuiNavigateTo& event) {
    auto cit = std::find_if(controllers_.begin(), controllers_.end(),
                            [&event](const auto& other) {
                              return event.unique_id == other->UniqueId();
                            });
    if (cit != controllers_.end()) {
      PushController(cit->get());
    }
  }

  void OnRosDomainIdChanged(
      const sensors_for_ros::event::RosDomainIdChanged& event) {
    StartRos(event.id, event.interface);
    PushController(&list_controller_);
  }

  void StartRos(int32_t ros_domain_id, std::string network_interface) {
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
    LOGI("Initalizing ROS");
    ros_.Initialize(ros_domain_id);
  }
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
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeSurfaceCreated(
    JNIEnv* env, jobject /*thiz*/, jobject surface) {
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (g_app && window) {
    LOGI("nativeSurfaceCreated: window=%p", window);
    g_app->gui_.Start(window);
  }
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeSurfaceDestroyed(
    JNIEnv* /*env*/, jobject /*thiz*/) {
  if (g_app) {
    LOGI("nativeSurfaceDestroyed");
    g_app->gui_.Stop();
  }
}

JNIEXPORT void JNICALL
Java_com_github_sloretz_sensors_1for_1ros_NativeBridge_nativeTouchEvent(
    JNIEnv* /*env*/, jobject /*thiz*/, jint action, jfloat x, jfloat y,
    jint tool_type) {
  if (g_app) {
    g_app->gui_.HandleTouchEvent(action, x, y, tool_type);
  }
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

  g_app->ros_domain_id_controller_.SetNetworkInterfaces(std::move(ifaces));
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

}  // extern "C"
