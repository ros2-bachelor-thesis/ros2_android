#include "jni/jni_object_utils.h"
#include "core/log.h"

namespace ros2_android {
namespace jni {

// Convert SensorType enum to string for Kotlin
static const char* SensorTypeToString(SensorType type) {
    switch (type) {
        case SensorType::ACCELEROMETER: return "accelerometer";
        case SensorType::BAROMETER: return "barometer";
        case SensorType::GPS: return "gps";
        case SensorType::GYROSCOPE: return "gyroscope";
        case SensorType::ILLUMINANCE: return "illuminance";
        case SensorType::MAGNETOMETER: return "magnetometer";
        case SensorType::UNKNOWN:
        default: return "unknown";
    }
}

jobject CreateSensorInfo(JNIEnv* env, const SensorInfoData& data) {
    if (!env) {
        LOGE("CreateSensorInfo: Invalid JNIEnv");
        return nullptr;
    }

    // Find SensorInfo class
    jclass sensorInfoClass = env->FindClass("com/github/mowerick/ros2/android/model/SensorInfo");
    if (!sensorInfoClass) {
        LOGE("Failed to find SensorInfo class");
        return nullptr;
    }

    // Find constructor: SensorInfo(String, String, String, String, String, String, Boolean)
    jmethodID constructor = env->GetMethodID(
        sensorInfoClass, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V");
    if (!constructor) {
        LOGE("Failed to find SensorInfo constructor");
        env->DeleteLocalRef(sensorInfoClass);
        return nullptr;
    }

    // Create Java strings
    jstring uniqueId = env->NewStringUTF(data.uniqueId.c_str());
    jstring prettyName = env->NewStringUTF(data.prettyName.c_str());
    jstring sensorName = env->NewStringUTF(data.sensorName.c_str());
    jstring vendor = env->NewStringUTF(data.vendor.c_str());
    jstring topicName = env->NewStringUTF(data.topicName.c_str());
    jstring topicType = env->NewStringUTF(data.topicType.c_str());

    // Create the object
    jobject sensorInfo = env->NewObject(
        sensorInfoClass, constructor,
        uniqueId, prettyName, sensorName, vendor, topicName, topicType,
        static_cast<jboolean>(data.enabled));

    // Clean up local references
    env->DeleteLocalRef(uniqueId);
    env->DeleteLocalRef(prettyName);
    env->DeleteLocalRef(sensorName);
    env->DeleteLocalRef(vendor);
    env->DeleteLocalRef(topicName);
    env->DeleteLocalRef(topicType);
    env->DeleteLocalRef(sensorInfoClass);

    return sensorInfo;
}

jobjectArray CreateSensorInfoArray(JNIEnv* env, const std::vector<SensorInfoData>& data) {
    if (!env) {
        LOGE("CreateSensorInfoArray: Invalid JNIEnv");
        return nullptr;
    }

    jclass sensorInfoClass = env->FindClass("com/github/mowerick/ros2/android/model/SensorInfo");
    if (!sensorInfoClass) {
        LOGE("Failed to find SensorInfo class");
        return nullptr;
    }

    jobjectArray array = env->NewObjectArray(
        static_cast<jsize>(data.size()),
        sensorInfoClass,
        nullptr);

    if (!array) {
        LOGE("Failed to create SensorInfo array");
        env->DeleteLocalRef(sensorInfoClass);
        return nullptr;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        jobject obj = CreateSensorInfo(env, data[i]);
        if (obj) {
            env->SetObjectArrayElement(array, static_cast<jsize>(i), obj);
            env->DeleteLocalRef(obj);
        }
    }

    env->DeleteLocalRef(sensorInfoClass);
    return array;
}

jobject CreateCameraInfo(JNIEnv* env, const CameraInfoData& data) {
    if (!env) {
        LOGE("CreateCameraInfo: Invalid JNIEnv");
        return nullptr;
    }

    // Find CameraInfo class
    jclass cameraInfoClass = env->FindClass("com/github/mowerick/ros2/android/model/CameraInfo");
    if (!cameraInfoClass) {
        LOGE("Failed to find CameraInfo class");
        return nullptr;
    }

    // Find constructor: CameraInfo(String, String, Boolean, String, String, String, String, String, String, Int, Int, Boolean, Int)
    jmethodID constructor = env->GetMethodID(
        cameraInfoClass, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;ZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZI)V");
    if (!constructor) {
        LOGE("Failed to find CameraInfo constructor");
        env->DeleteLocalRef(cameraInfoClass);
        return nullptr;
    }

    // Create Java strings
    jstring uniqueId = env->NewStringUTF(data.uniqueId.c_str());
    jstring name = env->NewStringUTF(data.name.c_str());
    jstring imageTopicName = env->NewStringUTF(data.imageTopicName.c_str());
    jstring imageTopicType = env->NewStringUTF(data.imageTopicType.c_str());
    jstring compressedImageTopicName = env->NewStringUTF(data.compressedImageTopicName.c_str());
    jstring compressedImageTopicType = env->NewStringUTF(data.compressedImageTopicType.c_str());
    jstring infoTopicName = env->NewStringUTF(data.infoTopicName.c_str());
    jstring infoTopicType = env->NewStringUTF(data.infoTopicType.c_str());

    // Create the object
    jobject cameraInfo = env->NewObject(
        cameraInfoClass, constructor,
        uniqueId, name,
        static_cast<jboolean>(data.enabled),
        imageTopicName, imageTopicType,
        compressedImageTopicName, compressedImageTopicType,
        infoTopicName, infoTopicType,
        static_cast<jint>(data.resolutionWidth),
        static_cast<jint>(data.resolutionHeight),
        static_cast<jboolean>(data.isFrontFacing),
        static_cast<jint>(data.sensorOrientation));

    // Clean up local references
    env->DeleteLocalRef(uniqueId);
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(imageTopicName);
    env->DeleteLocalRef(imageTopicType);
    env->DeleteLocalRef(compressedImageTopicName);
    env->DeleteLocalRef(compressedImageTopicType);
    env->DeleteLocalRef(infoTopicName);
    env->DeleteLocalRef(infoTopicType);
    env->DeleteLocalRef(cameraInfoClass);

    return cameraInfo;
}

jobjectArray CreateCameraInfoArray(JNIEnv* env, const std::vector<CameraInfoData>& data) {
    if (!env) {
        LOGE("CreateCameraInfoArray: Invalid JNIEnv");
        return nullptr;
    }

    jclass cameraInfoClass = env->FindClass("com/github/mowerick/ros2/android/model/CameraInfo");
    if (!cameraInfoClass) {
        LOGE("Failed to find CameraInfo class");
        return nullptr;
    }

    jobjectArray array = env->NewObjectArray(
        static_cast<jsize>(data.size()),
        cameraInfoClass,
        nullptr);

    if (!array) {
        LOGE("Failed to create CameraInfo array");
        env->DeleteLocalRef(cameraInfoClass);
        return nullptr;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        jobject obj = CreateCameraInfo(env, data[i]);
        if (obj) {
            env->SetObjectArrayElement(array, static_cast<jsize>(i), obj);
            env->DeleteLocalRef(obj);
        }
    }

    env->DeleteLocalRef(cameraInfoClass);
    return array;
}

jobject CreateSensorReading(JNIEnv* env, const SensorReadingData& data) {
    if (!env) {
        LOGE("CreateSensorReading: Invalid JNIEnv");
        return nullptr;
    }

    // Find List class and ArrayList implementation
    jclass arrayListClass = env->FindClass("java/util/ArrayList");
    if (!arrayListClass) {
        LOGE("Failed to find ArrayList class");
        return nullptr;
    }

    jmethodID arrayListConstructor = env->GetMethodID(arrayListClass, "<init>", "(I)V");
    jmethodID addMethod = env->GetMethodID(arrayListClass, "add", "(Ljava/lang/Object;)Z");

    if (!arrayListConstructor || !addMethod) {
        LOGE("Failed to find ArrayList methods");
        env->DeleteLocalRef(arrayListClass);
        return nullptr;
    }

    // Create ArrayList for values
    jobject valuesList = env->NewObject(arrayListClass, arrayListConstructor,
                                       static_cast<jint>(data.values.size()));
    if (!valuesList) {
        LOGE("Failed to create ArrayList");
        env->DeleteLocalRef(arrayListClass);
        return nullptr;
    }

    // Find Double class for boxing
    jclass doubleClass = env->FindClass("java/lang/Double");
    if (!doubleClass) {
        LOGE("Failed to find Double class");
        env->DeleteLocalRef(arrayListClass);
        env->DeleteLocalRef(valuesList);
        return nullptr;
    }

    jmethodID doubleConstructor = env->GetMethodID(doubleClass, "<init>", "(D)V");
    if (!doubleConstructor) {
        LOGE("Failed to find Double constructor");
        env->DeleteLocalRef(arrayListClass);
        env->DeleteLocalRef(valuesList);
        env->DeleteLocalRef(doubleClass);
        return nullptr;
    }

    // Add all values to the list
    for (double value : data.values) {
        jobject doubleObj = env->NewObject(doubleClass, doubleConstructor, value);
        if (doubleObj) {
            env->CallBooleanMethod(valuesList, addMethod, doubleObj);
            env->DeleteLocalRef(doubleObj);
        }
    }

    env->DeleteLocalRef(doubleClass);
    env->DeleteLocalRef(arrayListClass);

    // Find SensorType enum class
    jclass sensorTypeClass = env->FindClass("com/github/mowerick/ros2/android/model/SensorType");
    if (!sensorTypeClass) {
        LOGE("Failed to find SensorType class");
        env->DeleteLocalRef(valuesList);
        return nullptr;
    }

    // Get the fromString static method
    jmethodID fromStringMethod = env->GetStaticMethodID(
        sensorTypeClass, "fromString",
        "(Ljava/lang/String;)Lcom/github/mowerick/ros2/android/model/SensorType;");
    if (!fromStringMethod) {
        LOGE("Failed to find SensorType.fromString method");
        env->DeleteLocalRef(sensorTypeClass);
        env->DeleteLocalRef(valuesList);
        return nullptr;
    }

    // Convert sensor type string to SensorType enum
    jstring sensorTypeStr = env->NewStringUTF(SensorTypeToString(data.sensorType));
    jobject sensorTypeEnum = env->CallStaticObjectMethod(
        sensorTypeClass, fromStringMethod, sensorTypeStr);
    env->DeleteLocalRef(sensorTypeStr);
    env->DeleteLocalRef(sensorTypeClass);

    if (!sensorTypeEnum) {
        LOGE("Failed to create SensorType enum");
        env->DeleteLocalRef(valuesList);
        return nullptr;
    }

    // Find SensorReading class
    jclass sensorReadingClass = env->FindClass("com/github/mowerick/ros2/android/model/SensorReading");
    if (!sensorReadingClass) {
        LOGE("Failed to find SensorReading class");
        env->DeleteLocalRef(valuesList);
        env->DeleteLocalRef(sensorTypeEnum);
        return nullptr;
    }

    // Find constructor: SensorReading(List<Double>, String, SensorType)
    jmethodID constructor = env->GetMethodID(
        sensorReadingClass, "<init>",
        "(Ljava/util/List;Ljava/lang/String;Lcom/github/mowerick/ros2/android/model/SensorType;)V");
    if (!constructor) {
        LOGE("Failed to find SensorReading constructor");
        env->DeleteLocalRef(sensorReadingClass);
        env->DeleteLocalRef(valuesList);
        env->DeleteLocalRef(sensorTypeEnum);
        return nullptr;
    }

    // Create unit string
    jstring unit = env->NewStringUTF(data.unit.c_str());

    // Create the SensorReading object
    jobject sensorReading = env->NewObject(
        sensorReadingClass, constructor,
        valuesList, unit, sensorTypeEnum);

    // Clean up local references
    env->DeleteLocalRef(valuesList);
    env->DeleteLocalRef(unit);
    env->DeleteLocalRef(sensorTypeEnum);
    env->DeleteLocalRef(sensorReadingClass);

    return sensorReading;
}

jobjectArray CreateStringArray(JNIEnv* env, const std::vector<std::string>& strings) {
    if (!env) {
        LOGE("CreateStringArray: Invalid JNIEnv");
        return nullptr;
    }

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) {
        LOGE("Failed to find String class");
        return nullptr;
    }

    jobjectArray array = env->NewObjectArray(
        static_cast<jsize>(strings.size()),
        stringClass,
        nullptr);

    if (!array) {
        LOGE("Failed to create String array");
        env->DeleteLocalRef(stringClass);
        return nullptr;
    }

    for (size_t i = 0; i < strings.size(); ++i) {
        jstring str = env->NewStringUTF(strings[i].c_str());
        if (str) {
            env->SetObjectArrayElement(array, static_cast<jsize>(i), str);
            env->DeleteLocalRef(str);
        }
    }

    env->DeleteLocalRef(stringClass);
    return array;
}

} // namespace jni
} // namespace ros2_android
