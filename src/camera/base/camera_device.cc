#include "camera/base/camera_device.h"

#include <libyuv.h>

#include "core/notification_queue.h"
#include "core/time_utils.h"

using ros2_android::CameraDevice;

/// ***************** Android OpenCamera callbacks ********************

// Android camera state callback called when a camera is no longer available
void OnCameraDisconnected(void *context, ACameraDevice *device)
{
  LOGI("Camera Disconnected (context pointer %p, camera device pointer %p",
       context, device);
}

// Android camera state callback called when a camera is no longer available
// because of an error
void OnCameraError(void *context, ACameraDevice *device, int error)
{
  LOGI("Camera error (context pointer %p, camera device pointer %p, error %d)",
       context, device, error);
  ros2_android::PostNotification(
      ros2_android::NotificationSeverity::ERROR,
      "Camera error (code " + std::to_string(error) + ")");
}

static const ACameraDevice_stateCallbacks kCameraStateCallbacks = {
    .context = nullptr,
    .onDisconnected = OnCameraDisconnected,
    .onError = OnCameraError,
};

/// ***************** Android AImageReader callbacks ********************

void OnImage(void *context, AImageReader *reader)
{
  LOGI("OnImage callback triggered!");
  AImage *cimage = nullptr;
  auto status = AImageReader_acquireNextImage(reader, &cimage);

  if (status != AMEDIA_OK)
  {
    LOGW("Failed to acquire image, status: %d", status);
    return;
  }

  std::unique_ptr<AImage, ros2_android::AImageDeleter> image(cimage);

  auto *camera_device = static_cast<ros2_android::CameraDevice *>(context);
  camera_device->OnImage(std::move(image));
}

static const AImageReader_ImageListener kImageListenerCallbacks = {
    .context = nullptr,
    .onImageAvailable = OnImage,
};

/// ***************** Android capture session state callbacks
/// ********************
// TODO implement these -they're still copy/pasted
static void onSessionActive(void *context, ACameraCaptureSession *session) {}

static void onSessionReady(void *context, ACameraCaptureSession *session) {}

static void onSessionClosed(void *context, ACameraCaptureSession *session) {}

static ACameraCaptureSession_stateCallbacks sessionStateCallbacks{
    .context = nullptr,
    .onClosed = onSessionClosed,
    .onReady = onSessionReady,
    .onActive = onSessionActive,
};

/// ***************** Android capture callbacks ********************
// TODO implement these -they're still copy/pasted
void onCaptureFailed(void *context, ACameraCaptureSession *session,
                     ACaptureRequest *request, ACameraCaptureFailure *failure)
{
}

void onCaptureSequenceCompleted(void *context, ACameraCaptureSession *session,
                                int sequenceId, int64_t frameNumber) {}

void onCaptureSequenceAborted(void *context, ACameraCaptureSession *session,
                              int sequenceId) {}

void onCaptureCompleted(void *context, ACameraCaptureSession *session,
                        ACaptureRequest *request,
                        const ACameraMetadata *result) {}

static ACameraCaptureSession_captureCallbacks captureCallbacks{
    .context = nullptr,
    .onCaptureStarted = nullptr,
    .onCaptureProgressed = nullptr,
    .onCaptureCompleted = onCaptureCompleted,
    .onCaptureFailed = onCaptureFailed,
    .onCaptureSequenceCompleted = onCaptureSequenceCompleted,
    .onCaptureSequenceAborted = onCaptureSequenceAborted,
    .onCaptureBufferLost = nullptr,
};

/// ***************** CameraDevice ********************
std::unique_ptr<CameraDevice> CameraDevice::OpenCamera(
    ACameraManager *native_manager, const CameraDescriptor &desc, const std::string& device_id)
{
  const char *camera_id = desc.id.c_str();
  auto camera_device = std::unique_ptr<CameraDevice>(new CameraDevice);
  camera_device->desc_ = desc;
  camera_device->device_id_ = device_id;

  auto result = ACameraManager_openCamera(native_manager, camera_id,
                                          &(camera_device->state_callbacks_),
                                          &(camera_device->native_device_));

  if (ACAMERA_OK != result)
  {
    LOGW("Failed to open camera %s, %d", camera_id, result);
    return nullptr;
  }

  // Open image reader to get camera data
  constexpr int max_simultaneous_images = 1;
  media_status_t status = AImageReader_new(
      camera_device->width_, camera_device->height_, AIMAGE_FORMAT_YUV_420_888,
      max_simultaneous_images, &(camera_device->reader_));

  // TODO handle errors
  // if (status != AMEDIA_OK)
  // Handle errors here

  AImageReader_setImageListener(camera_device->reader_,
                                &(camera_device->reader_callbacks_));

  ACaptureSessionOutputContainer_create(&(camera_device->output_container_));

  ACameraDevice_createCaptureRequest(camera_device->native_device_,
                                     TEMPLATE_RECORD,
                                     &(camera_device->capture_request_));

  ANativeWindow *native_window;
  AImageReader_getWindow(camera_device->reader_, &native_window);
  ANativeWindow_acquire(native_window);
  ACameraOutputTarget_create(native_window,
                             &(camera_device->camera_output_target_));
  ACaptureRequest_addTarget(camera_device->capture_request_,
                            camera_device->camera_output_target_);
  ACaptureSessionOutput_create(native_window,
                               &(camera_device->capture_session_output_));
  ACaptureSessionOutputContainer_add(camera_device->output_container_,
                                     camera_device->capture_session_output_);

  ACameraDevice_createCaptureSession(camera_device->native_device_,
                                     camera_device->output_container_,
                                     &sessionStateCallbacks, // TODO
                                     &(camera_device->capture_session_));

  // Start Recording
  ACameraCaptureSession_setRepeatingRequest(
      camera_device->capture_session_,
      &captureCallbacks, // TODO
      1, &(camera_device->capture_request_), nullptr);

  return camera_device;
}

CameraDevice::CameraDevice()
    : state_callbacks_(kCameraStateCallbacks),
      reader_callbacks_(kImageListenerCallbacks)
{
  state_callbacks_.context = this;
  reader_callbacks_.context = this;

  shutdown_.store(false);
  thread_ = std::thread(&CameraDevice::ProcessImages, this);
}

void CameraDevice::ProcessImages()
{
  while (!shutdown_.load())
  {
    std::unique_ptr<AImage, AImageDeleter> image;
    {
      // Wait for next image, or shutdown
      std::unique_lock guard(mutex_);
      wake_cv_.wait(guard, [&image, this]
                    {
        image = std::move(image_);
        image_.reset();
        return nullptr != image.get() || shutdown_.load(); });
    }
    if (nullptr != image.get())
    {
      // Get image timestamp (nanoseconds since boot, same timebase as sensors)
      int64_t image_timestamp_ns = 0;
      if (AMEDIA_OK != AImage_getTimestamp(image.get(), &image_timestamp_ns))
      {
        LOGW("Unable to get image timestamp, using current time");
        image_timestamp_ns = 0;
      }

      // Convert from CLOCK_BOOTTIME to ROS epoch time (CLOCK_REALTIME)
      int64_t offset_ns = ros2_android::time_utils::GetBootTimeOffsetNs();
      int64_t ros_epoch_timestamp_ns = image_timestamp_ns + offset_ns;

      // Get YUV plane metadata
      int32_t y_row_stride;
      int32_t uv_pixel_stride;
      int32_t uv_row_stride;

      if (AMEDIA_OK != AImage_getPlaneRowStride(image.get(), 0, &y_row_stride))
      {
        LOGW("Unable to get Y plane row stride");
        continue;
      }

      if (AMEDIA_OK != AImage_getPlanePixelStride(image.get(), 1, &uv_pixel_stride))
      {
        LOGW("Unable to get U/V plane pixel stride");
        continue;
      }

      if (AMEDIA_OK != AImage_getPlaneRowStride(image.get(), 1, &uv_row_stride))
      {
        LOGW("Unable to get U/V plane row stride");
        continue;
      }

      // Get YUV plane data pointers
      uint8_t *y_data = nullptr;
      uint8_t *u_data = nullptr;
      uint8_t *v_data = nullptr;
      int y_len = -1;
      int u_len = -1;
      int v_len = -1;

      if (AMEDIA_OK != AImage_getPlaneData(image.get(), 0, &y_data, &y_len))
      {
        LOGW("Unable to get Y plane data");
        continue;
      }

      if (AMEDIA_OK != AImage_getPlaneData(image.get(), 1, &u_data, &u_len))
      {
        LOGW("Unable to get U plane data");
        continue;
      }

      if (AMEDIA_OK != AImage_getPlaneData(image.get(), 2, &v_data, &v_len))
      {
        LOGW("Unable to get V plane data");
        continue;
      }

      // Convert YUV420 to I420 and rotate in one step
      // sensor_orientation is the clockwise angle to rotate for correct display orientation
      libyuv::RotationMode rotation;
      switch (desc_.sensor_orientation)
      {
      case 0:
        rotation = libyuv::kRotate0;
        break;
      case 90:
        rotation = libyuv::kRotate90;
        break;
      case 180:
        rotation = libyuv::kRotate180;
        break;
      case 270:
        rotation = libyuv::kRotate270;
        break;
      default:
        LOGW("Unexpected sensor_orientation %d, defaulting to 0°", desc_.sensor_orientation);
        rotation = libyuv::kRotate0;
        break;
      }

      // Calculate output dimensions after rotation
      int output_width = (rotation == libyuv::kRotate90 || rotation == libyuv::kRotate270) ? height_ : width_;
      int output_height = (rotation == libyuv::kRotate90 || rotation == libyuv::kRotate270) ? width_ : height_;

      // Allocate I420 buffers for rotated YUV image
      int i420_y_size = output_width * output_height;
      int i420_uv_size = ((output_width + 1) / 2) * ((output_height + 1) / 2);
      std::vector<uint8_t> rotated_y(i420_y_size);
      std::vector<uint8_t> rotated_u(i420_uv_size);
      std::vector<uint8_t> rotated_v(i420_uv_size);

      // Convert and rotate YUV420 to I420 in one step using libyuv
      int result = libyuv::Android420ToI420Rotate(
          y_data, y_row_stride,
          u_data, uv_row_stride,
          v_data, uv_row_stride,
          uv_pixel_stride,
          rotated_y.data(), output_width,
          rotated_u.data(), (output_width + 1) / 2,
          rotated_v.data(), (output_width + 1) / 2,
          width_, height_,
          rotation);

      if (result != 0)
      {
        LOGW("libyuv Android420ToI420Rotate failed with code %d", result);
        continue;
      }

      // Convert rotated I420 to RGB24
      std::vector<uint8_t> rgb_buffer(output_width * output_height * 3);
      result = libyuv::I420ToRGB24(
          rotated_y.data(), output_width,
          rotated_u.data(), (output_width + 1) / 2,
          rotated_v.data(), (output_width + 1) / 2,
          rgb_buffer.data(), output_width * 3,
          output_width, output_height);

      if (result != 0)
      {
        LOGW("libyuv I420->RGB24 conversion failed with code %d", result);
        continue;
      }

      // Create ROS2 Image message with BGR8 encoding (libyuv RGB24 outputs BGR)
      auto image_msg = std::make_unique<sensor_msgs::msg::Image>();

      // Set timestamp from camera capture time (already converted to ROS epoch time above)
      image_msg->header.stamp.sec = static_cast<int32_t>(ros_epoch_timestamp_ns / 1000000000LL);
      image_msg->header.stamp.nanosec = static_cast<uint32_t>(ros_epoch_timestamp_ns % 1000000000LL);
      image_msg->header.frame_id = device_id_ + "_" + desc_.id;
      image_msg->width = output_width;
      image_msg->height = output_height;
      image_msg->encoding = "bgr8";
      image_msg->step = output_width * 3;
      image_msg->data = std::move(rgb_buffer);

      // Populate camera_info with estimated intrinsics
      auto camera_info = std::make_unique<CameraInfo>();
      camera_info->header = image_msg->header; // Use same timestamp and frame_id
      camera_info->width = output_width;
      camera_info->height = output_height;
      camera_info->distortion_model = "plumb_bob";

      // Distortion coefficients (k1, k2, t1, t2, k3) - zeros for uncalibrated
      camera_info->d.resize(5, 0.0);

      // Camera intrinsic matrix K [3x3]
      // Estimate focal length as ~0.8 * image_width (typical for mobile cameras)
      // This is a reasonable approximation without actual calibration
      double fx = output_width * 0.8;
      double fy = output_width * 0.8;
      double cx = output_width / 2.0;
      double cy = output_height / 2.0;

      camera_info->k[0] = fx;
      camera_info->k[1] = 0.0;
      camera_info->k[2] = cx;
      camera_info->k[3] = 0.0;
      camera_info->k[4] = fy;
      camera_info->k[5] = cy;
      camera_info->k[6] = 0.0;
      camera_info->k[7] = 0.0;
      camera_info->k[8] = 1.0;

      // Rectification matrix R [3x3] - identity for monocular camera
      camera_info->r[0] = 1.0;
      camera_info->r[1] = 0.0;
      camera_info->r[2] = 0.0;
      camera_info->r[3] = 0.0;
      camera_info->r[4] = 1.0;
      camera_info->r[5] = 0.0;
      camera_info->r[6] = 0.0;
      camera_info->r[7] = 0.0;
      camera_info->r[8] = 1.0;

      // Projection matrix P [3x4] - for monocular, derived from K with no translation
      camera_info->p[0] = fx;
      camera_info->p[1] = 0.0;
      camera_info->p[2] = cx;
      camera_info->p[3] = 0.0;
      camera_info->p[4] = 0.0;
      camera_info->p[5] = fy;
      camera_info->p[6] = cy;
      camera_info->p[7] = 0.0;
      camera_info->p[8] = 0.0;
      camera_info->p[9] = 0.0;
      camera_info->p[10] = 1.0;
      camera_info->p[11] = 0.0;

      Emit({std::move(camera_info), std::move(image_msg)});
    }
  }
  LOGI("Camera device ProcessImages shutting down");
}

CameraDevice::~CameraDevice()
{
  // Shut down processing thread
  shutdown_.store(true);
  if (thread_.joinable())
  {
    wake_cv_.notify_one();
    thread_.join();
  }
  if (capture_session_)
  {
    ACameraCaptureSession_stopRepeating(capture_session_);
    ACameraCaptureSession_close(capture_session_);
  }

  if (output_container_)
  {
    ACaptureSessionOutputContainer_free(output_container_);
  }

  if (capture_session_output_)
  {
    ACaptureSessionOutput_free(capture_session_output_);
  }

  if (capture_request_)
  {
    ACaptureRequest_free(capture_request_);
  }

  if (reader_)
  {
    AImageReader_delete(reader_);
  }
  if (native_device_)
  {
    ACameraDevice_close(native_device_);
  }
}

void CameraDevice::OnImage(std::unique_ptr<AImage, AImageDeleter> image)
{
  LOGI("CameraDevice::OnImage - handing off to processing thread");
  // Hand off image data to processing thread
  std::unique_lock lock(mutex_);
  image_ = std::move(image);
  wake_cv_.notify_one();
}
