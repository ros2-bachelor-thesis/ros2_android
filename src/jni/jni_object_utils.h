#ifndef ROS2_ANDROID_JNI_OBJECT_UTILS_H
#define ROS2_ANDROID_JNI_OBJECT_UTILS_H

#include <jni.h>
#include <string>
#include <vector>

namespace ros2_android {
namespace jni {

// Sensor type enumeration matching Kotlin SensorType enum
enum class SensorType {
    ACCELEROMETER,
    BAROMETER,
    GPS,
    GYROSCOPE,
    ILLUMINANCE,
    MAGNETOMETER,
    UNKNOWN
};

// SensorInfo structure matching Kotlin data class
struct SensorInfoData {
    std::string uniqueId;
    std::string prettyName;
    std::string sensorName;
    std::string vendor;
    std::string topicName;
    std::string topicType;
    bool enabled;
};

// CameraInfo structure matching Kotlin data class
struct CameraInfoData {
    std::string uniqueId;
    std::string name;
    bool enabled;
    std::string imageTopicName;
    std::string imageTopicType;
    std::string compressedImageTopicName;
    std::string compressedImageTopicType;
    std::string infoTopicName;
    std::string infoTopicType;
    int resolutionWidth;
    int resolutionHeight;
    bool isFrontFacing;
    int sensorOrientation;
};

// SensorReading structure matching Kotlin data class
struct SensorReadingData {
    std::vector<double> values;
    std::string unit;
    SensorType sensorType;
};

// ExternalDeviceInfo structure matching Kotlin data class
struct ExternalDeviceInfoData {
    std::string uniqueId;
    std::string name;
    std::string deviceType;  // "LIDAR" or "USB_CAMERA"
    std::string usbPath;
    int vendorId;
    int productId;
    std::string topicName;
    std::string topicType;
    bool connected;
    bool enabled;
};

// Create a SensorInfo Kotlin object from native data
jobject CreateSensorInfo(JNIEnv* env, const SensorInfoData& data);

// Create an array of SensorInfo objects
jobjectArray CreateSensorInfoArray(JNIEnv* env, const std::vector<SensorInfoData>& data);

// Create a CameraInfo Kotlin object from native data
jobject CreateCameraInfo(JNIEnv* env, const CameraInfoData& data);

// Create an array of CameraInfo objects
jobjectArray CreateCameraInfoArray(JNIEnv* env, const std::vector<CameraInfoData>& data);

// Create a SensorReading Kotlin object from native data
jobject CreateSensorReading(JNIEnv* env, const SensorReadingData& data);

// Create a String array from vector<string>
jobjectArray CreateStringArray(JNIEnv* env, const std::vector<std::string>& strings);

// Create an ExternalDeviceInfo Kotlin object from native data
jobject CreateExternalDeviceInfo(JNIEnv* env, const ExternalDeviceInfoData& data);

// Create an array of ExternalDeviceInfo objects
jobjectArray CreateExternalDeviceInfoArray(JNIEnv* env, const std::vector<ExternalDeviceInfoData>& data);

} // namespace jni
} // namespace ros2_android

#endif // ROS2_ANDROID_JNI_OBJECT_UTILS_H
