#include "camera/camera_manager.h"

#include <media/NdkImage.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include "core/log.h"

using ros2_android::CameraDescriptor;
using ros2_android::CameraDevice;
using ros2_android::CameraManager;

/// ***************** Camera manager stuff ********************
CameraManager::CameraManager()
{
  native_manager_ = ACameraManager_create();

  DiscoverCameras();
}

void CameraManager::DiscoverCameras()
{
  cameras_.clear();
  // Get camera IDs and put them in RAII container
  std::unique_ptr<ACameraIdList, decltype(&ACameraManager_deleteCameraIdList)>
      camera_ids{nullptr, nullptr};
  {
    ACameraIdList *camera_ids_temp = nullptr;
    ACameraManager_getCameraIdList(native_manager_, &camera_ids_temp);
    camera_ids = {camera_ids_temp, &ACameraManager_deleteCameraIdList};
  }

  // First pass: collect physical sub-camera IDs from logical multi-camera
  // devices so we can skip them, and record which top-level camera IDs are
  // logical multi-camera devices. On modern Android (API 28+) physical-only
  // sub-cameras are already hidden from getCameraIdList, but some OEMs still
  // expose standalone cameras that share the same facing direction (e.g. wide
  // + ultrawide as separate entries). We deduplicate those in a later pass.
  std::set<std::string> physical_sub_camera_ids;
  std::set<std::string> logical_multi_camera_ids;
  for (int i = 0; i < camera_ids->numCameras; ++i)
  {
    ACameraMetadata *metadata_temp = nullptr;
    ACameraManager_getCameraCharacteristics(
        native_manager_, camera_ids->cameraIds[i], &metadata_temp);
    if (!metadata_temp)
      continue;
    std::unique_ptr<ACameraMetadata, decltype(&ACameraMetadata_free)> metadata{
        metadata_temp, &ACameraMetadata_free};

    ACameraMetadata_const_entry caps_entry = {0};
    auto status = ACameraMetadata_getConstEntry(
        metadata.get(), ACAMERA_REQUEST_AVAILABLE_CAPABILITIES, &caps_entry);
    if (ACAMERA_OK != status)
      continue;

    bool is_logical_multi = false;
    for (uint32_t j = 0; j < caps_entry.count; ++j)
    {
      if (caps_entry.data.u8[j] ==
          ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA)
      {
        is_logical_multi = true;
        break;
      }
    }

    if (is_logical_multi)
    {
      LOGI("Camera %s is a logical multi-camera device",
           camera_ids->cameraIds[i]);
      logical_multi_camera_ids.insert(camera_ids->cameraIds[i]);

      ACameraMetadata_const_entry phys_entry = {0};
      status = ACameraMetadata_getConstEntry(
          metadata.get(), ACAMERA_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
          &phys_entry);
      if (ACAMERA_OK != status)
        continue;

      // Physical IDs are stored as consecutive null-terminated strings
      const uint8_t *ptr = phys_entry.data.u8;
      const uint8_t *end = ptr + phys_entry.count;
      while (ptr < end)
      {
        std::string phys_id(reinterpret_cast<const char *>(ptr));
        if (!phys_id.empty())
        {
          LOGI("Camera %s has physical sub-camera %s",
               camera_ids->cameraIds[i], phys_id.c_str());
          physical_sub_camera_ids.insert(phys_id);
        }
        ptr += phys_id.size() + 1;
      }
    }
    else
    {
      LOGI("Camera %s is a standalone (non-logical) camera",
           camera_ids->cameraIds[i]);
    }
  }

  // Second pass: enumerate cameras, skipping physical sub-cameras
  for (int i = 0; i < camera_ids->numCameras; ++i)
  {
    std::string cam_id = camera_ids->cameraIds[i];
    if (physical_sub_camera_ids.count(cam_id))
    {
      LOGI("Skipping physical sub-camera %s", cam_id.c_str());
      continue;
    }

    CameraDescriptor cam_desc;
    cam_desc.id = cam_id;
    std::unique_ptr<ACameraMetadata, decltype(&ACameraMetadata_free)> metadata{
        nullptr, nullptr};
    {
      ACameraMetadata *metadata_temp;
      ACameraManager_getCameraCharacteristics(
          native_manager_, cam_desc.id.c_str(), &metadata_temp);
      metadata = {metadata_temp, &ACameraMetadata_free};
    }

    ACameraMetadata_const_entry entry = {0};

    auto status = ACameraMetadata_getConstEntry(metadata.get(),
                                                ACAMERA_LENS_FACING, &entry);
    if (ACAMERA_OK != status)
    {
      LOGW("Unable to get ACAMERA_LENS_FACING from camera %d", i);
      continue;
    }
    cam_desc.lens_facing =
        static_cast<acamera_metadata_enum_android_lens_facing_t>(
            entry.data.u8[0]);
    cam_desc.is_logical_multi_camera =
        logical_multi_camera_ids.count(cam_id) > 0;

    // Read sensor orientation (clockwise degrees to match device portrait)
    ACameraMetadata_const_entry orient_entry = {0};
    status = ACameraMetadata_getConstEntry(metadata.get(),
                                           ACAMERA_SENSOR_ORIENTATION,
                                           &orient_entry);
    if (ACAMERA_OK == status && orient_entry.count > 0)
    {
      cam_desc.sensor_orientation = orient_entry.data.i32[0];
      LOGI("Camera %s sensor orientation: %d", cam_id.c_str(),
           cam_desc.sensor_orientation);
    }

    LOGI("Looking up available stream configurations");
    // Figure out what kinds of data we can get from the camera
    ACameraMetadata_getConstEntry(
        metadata.get(), ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
    for (int j = 0; j < entry.count; j += 4)
    {
      // We are only interested in output streams, so skip input stream
      int32_t input = entry.data.i32[j + 3];
      if (input)
      {
        continue;
      }

      int32_t format = entry.data.i32[j + 0];
      if (format == AIMAGE_FORMAT_YUV_420_888)
      {
        // This one is always supported.
        // https://developer.android.com/ndk/reference/group/media
        // #group___media_1gga9c3dace30485a0f28163a882a5d65a19aea9797f9b5db5d26a2055a43d8491890
        int32_t width = entry.data.i32[j + 1];
        int32_t height = entry.data.i32[j + 2];
        LOGI("YUV_420_888 supported w: %d h: %d", width, height);
      }
    }

    LOGI("Discovered camera: %s (facing=%d, logical_multi=%d)",
         cam_desc.GetName().c_str(), cam_desc.lens_facing,
         cam_desc.is_logical_multi_camera);
    cameras_.push_back(cam_desc);
  }

  // Third pass: deduplicate by facing direction. On many devices (API 28+)
  // physical sub-cameras are already hidden, but the device may still list
  // multiple standalone cameras per facing direction (e.g. wide + ultrawide).
  // For each facing direction we keep only one camera, preferring the logical
  // multi-camera device (which fuses the physical sensors), or the first
  // (lowest-numbered) camera if none are logical.
  std::map<acamera_metadata_enum_acamera_lens_facing, CameraDescriptor>
      best_per_facing;
  for (const auto &cam : cameras_)
  {
    auto it = best_per_facing.find(cam.lens_facing);
    if (it == best_per_facing.end())
    {
      // First camera with this facing - keep it
      best_per_facing.emplace(cam.lens_facing, cam);
    }
    else if (cam.is_logical_multi_camera &&
             !it->second.is_logical_multi_camera)
    {
      // Prefer the logical multi-camera over a standalone camera
      it->second = cam;
    }
    // Otherwise keep the first one (lowest ID) for this facing direction
  }

  cameras_.clear();
  for (auto &[facing, cam] : best_per_facing)
  {
    cameras_.push_back(std::move(cam));
  }

  // Build display names: count cameras per facing direction first
  std::map<acamera_metadata_enum_acamera_lens_facing, int> facing_counts;
  for (const auto &cam : cameras_)
  {
    facing_counts[cam.lens_facing]++;
  }

  std::map<acamera_metadata_enum_acamera_lens_facing, int> facing_index;
  for (auto &cam : cameras_)
  {
    std::string label;
    std::string topic_slug;
    switch (cam.lens_facing)
    {
    case ACAMERA_LENS_FACING_FRONT:
      label = "Front Camera";
      topic_slug = "front";
      break;
    case ACAMERA_LENS_FACING_BACK:
      label = "Rear Camera";
      topic_slug = "rear";
      break;
    case ACAMERA_LENS_FACING_EXTERNAL:
      label = "External Camera";
      topic_slug = "external";
      break;
    default:
      label = "Camera";
      topic_slug = "camera_" + cam.id;
      break;
    }
    int count = facing_counts[cam.lens_facing];
    if (count > 1)
    {
      int idx = ++facing_index[cam.lens_facing];
      label += " " + std::to_string(idx);
      topic_slug += "_" + std::to_string(idx);
    }
    cam.display_name = label;
    cam.topic_prefix = "camera/" + topic_slug + "/";
    LOGI("Selected camera: %s (topics: %s)", cam.display_name.c_str(),
         cam.topic_prefix.c_str());
  }
}

CameraManager::~CameraManager()
{
  if (native_manager_)
  {
    ACameraManager_delete(native_manager_);
  }
}

std::unique_ptr<CameraDevice> CameraManager::OpenCamera(
    const CameraDescriptor &desc, const std::string& device_id) const
{
  return std::move(CameraDevice::OpenCamera(native_manager_, desc, device_id));
}
