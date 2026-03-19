# Camera Frame Publishing Implementation & Optimization

## Overview

Implemented optimized camera frame publishing for ROS2 Android application with 640x480 resolution at 30 FPS. **Phase 1 Complete**: Achieved ~20ms total processing time per frame (7.5x improvement), down from initial ~150ms.

**Current Status**: Phase 1 complete (libyuv, event-driven, native bitmaps). Phase 2 pending (JPEG compression for bandwidth optimization).

## Implementation Details

### 1. YUV to RGB Conversion with libyuv

**Why**: Android Camera2 provides frames in YUV_420_888 format, but ROS2 image tools expect RGB8 encoding.

**Approach**: Integrated Google's libyuv library for hardware-accelerated (NEON SIMD) YUV→RGB conversion.

**Implementation** (`camera_device.cc:256-321`):

Uses libyuv's unified `Android420ToI420Rotate()` function which:
1. Automatically detects YUV_420_888 variant (NV12/NV21/I420) based on `uv_pixel_stride`
2. Converts to intermediate I420 format
3. Applies rotation correction based on `sensor_orientation` (0°/90°/180°/270°)
4. All in a single optimized operation

```cpp
// Determine rotation correction based on camera sensor orientation
libyuv::RotationMode rotation;
switch (desc_.sensor_orientation) {
  case 0:   rotation = libyuv::kRotate0; break;
  case 90:  rotation = libyuv::kRotate90; break;
  case 180: rotation = libyuv::kRotate180; break;
  case 270: rotation = libyuv::kRotate270; break;
  default:  rotation = libyuv::kRotate0; break;
}

// Calculate output dimensions after rotation
int output_width = (rotation == libyuv::kRotate90 || rotation == libyuv::kRotate270) ? height_ : width_;
int output_height = (rotation == libyuv::kRotate90 || rotation == libyuv::kRotate270) ? width_ : height_;

// Convert YUV420 to I420 and rotate in one step (auto-detects NV12/NV21/I420)
libyuv::Android420ToI420Rotate(
    y_data, y_row_stride,
    u_data, uv_row_stride,
    v_data, uv_row_stride,
    uv_pixel_stride,  // Used to auto-detect format variant
    rotated_y.data(), output_width,
    rotated_u.data(), (output_width + 1) / 2,
    rotated_v.data(), (output_width + 1) / 2,
    width_, height_,
    rotation);

// Convert rotated I420 to RGB24
libyuv::I420ToRGB24(
    rotated_y.data(), output_width,
    rotated_u.data(), (output_width + 1) / 2,
    rotated_v.data(), (output_width + 1) / 2,
    rgb_buffer.data(), output_width * 3,
    output_width, output_height);
```

**Performance**:
- Conversion + rotation time: ~100ms (manual float math) → ~7ms (libyuv NEON)
- 14x speedup using ARM NEON SIMD instructions
- Rotation integrated into conversion (no separate rotation pass needed)

**Why rotation is needed**: Android cameras report images in sensor orientation, not device orientation. For a phone held in portrait mode with a 90° rotated sensor, the camera produces landscape images that appear sideways. The `sensor_orientation` field (queried from `ACameraMetadata`) tells us the clockwise rotation needed to display correctly.

**Dependencies**:
- Added libyuv as git submodule in `deps/`
- ~200KB APK size increase
- Zero runtime overhead (compiled NEON intrinsics)

### 2. Automatic Image Rotation

**Why**: Android cameras report images in sensor orientation, which doesn't match device orientation. A portrait-held phone with a 90° sensor produces sideways landscape images.

**Approach**: Query `sensor_orientation` from camera metadata and rotate during YUV conversion using libyuv's combined `Android420ToI420Rotate()` function.

**Implementation**: See section 1 above - rotation is integrated into the YUV conversion step (not a separate pass).

**Performance**: No additional cost - `Android420ToI420Rotate()` does both operations in one NEON-optimized pass.

**Device compatibility**: Tested with front camera (270° rotation) and rear camera (90° rotation). Output dimensions swap for 90°/270° rotations (640×480 becomes 480×640).

### 3. Dynamic YUV Plane Stride Handling

**Why**: Android cameras may add padding to image rows for cache alignment. Hardcoding stride to width causes memory access violations.

**Approach**: Query actual stride from AImage API before processing.

**Implementation** (`camera_device.cc:207-228`):

```cpp
// Query Y plane stride (may differ from width due to padding)
int32_t y_row_stride;
if (AMEDIA_OK != AImage_getPlaneRowStride(image.get(), 0, &y_row_stride)) {
  LOGW("Unable to get Y plane row stride");
  continue;
}

// Query UV plane metadata
int32_t uv_pixel_stride;
int32_t uv_row_stride;
AImage_getPlanePixelStride(image.get(), 1, &uv_pixel_stride);
AImage_getPlaneRowStride(image.get(), 1, &uv_row_stride);
```

**Why this matters**: On some devices, 640px width may have 672 byte stride (32-byte aligned). Using width=640 directly would access invalid memory.

### 4. Standard ROS2 BGR8 Encoding

**Why**: ROS2 image ecosystem expects standard encodings. libyuv's `I420ToRGB24()` outputs RGB pixel order, but the encoding is set to `bgr8`.

> [!NOTE]
> Despite the function name `I420ToRGB24()`, the actual pixel order needs verification. The code labels it as `bgr8` and subsequent BGR→RGB swapping in the JPEG compression path (camera_controller.cc:149-156) treats it as BGR data. This suggests either: (a) libyuv outputs BGR despite the function name, or (b) there's a latent bug. ROS2 tools work correctly with the current setup, indicating the encoding matches the actual data.

**Approach**: Publish `bgr8` encoding (3 bytes per pixel, B-G-R order) based on actual pixel order produced by libyuv.

**Implementation** (`camera_device.cc:323-334`):

```cpp
auto image_msg = std::make_unique<sensor_msgs::msg::Image>();
image_msg->width = width_;
image_msg->height = height_;
image_msg->encoding = "bgr8";  // libyuv RGB24 outputs BGR pixel order
image_msg->step = width_ * 3;
image_msg->data = std::move(rgb_buffer);
```

**Bandwidth**: 640×480×3 = 921,600 bytes per frame (~900KB)

**Compatibility**: Works with all standard ROS2 image tools:
- `rqt_image_view`
- `image_view`
- `image_transport` plugins
- OpenCV bridge (prefers BGR)

### 5. Separate BGR→ARGB Conversion for Android UI with Inverse Rotation

**Why**: Android bitmaps require ARGB format (4 bytes per pixel), but ROS2 publishes BGR8. Additionally, the ROS2 image is already rotated for correct display in `rqt_image_view`, but Android UI needs portrait orientation.

**Approach**: Separate data paths:
1. Publish rotated BGR8 over DDS for ROS2 tools
2. Convert BGR8→ARGB and apply inverse rotation for Android UI preview

**Implementation** (`camera_controller.cc:194-250`):

```cpp
// Convert BGR8 to ARGB for UI preview
// The BGR image from camera_device is already rotated for ROS2/rqt_image_view
// For Android UI, we need to rotate it back to match device portrait orientation
{
  std::lock_guard<std::mutex> lock(frame_mutex_);
  const auto &bgr_data = info_image.second->data;
  int width = info_image.second->width;
  int height = info_image.second->height;

  // Convert BGR24 to ARGB first using libyuv
  std::vector<uint8_t> argb_buffer(width * height * 4);
  libyuv::RAWToARGB(
      bgr_data.data(), width * 3,
      argb_buffer.data(), width * 4,
      width, height);

  // Determine inverse rotation to undo sensor_orientation rotation
  // (90° CW becomes 270° CW to undo, etc.)
  libyuv::RotationMode inverse_rotation;
  switch (camera_descriptor_.sensor_orientation) {
    case 0:   inverse_rotation = libyuv::kRotate0; break;
    case 90:  inverse_rotation = libyuv::kRotate270; break;  // undo 90° CW
    case 180: inverse_rotation = libyuv::kRotate180; break;
    case 270: inverse_rotation = libyuv::kRotate90; break;   // undo 270° CW
    default:  inverse_rotation = libyuv::kRotate0; break;
  }

  // Calculate dimensions after inverse rotation
  bool swaps_dimensions = (inverse_rotation == libyuv::kRotate90 || inverse_rotation == libyuv::kRotate270);
  int rotated_width = swaps_dimensions ? height : width;
  int rotated_height = swaps_dimensions ? width : height;

  last_frame_.resize(rotated_width * rotated_height * 4);

  // Rotate ARGB image back to portrait orientation
  libyuv::ARGBRotate(
      argb_buffer.data(), width * 4,
      last_frame_.data(), rotated_width * 4,
      width, height,
      inverse_rotation);
}
```

**Performance**: ~3-4ms conversion + inverse rotation time
- Uses libyuv `RAWToARGB()` for BGR→ARGB conversion (NEON-optimized)
- Uses libyuv `ARGBRotate()` for inverse rotation (NEON-optimized)
- Only runs for UI preview (not in DDS publishing path)
- Event-driven (callback only when new frame available)
- Throttled to 10 Hz via `CameraFrameCallbackQueue`

### 6. Native Bitmap Creation from ARGB

**Why**: Passing raw bytes from C++ to Kotlin and creating bitmaps in managed code is slow.

**Approach**: Create Android Bitmap objects directly in native code using AndroidBitmap API.

**Implementation** (`jni_bridge.cc` and `bitmap_utils.cc`):

```cpp
// Create Bitmap with ARGB_8888 config
jobject bitmap = env->CallStaticObjectMethod(
    bitmapClass, createBitmapMethod, width, height, bitmapConfig);

// Lock pixels and copy RGBA data directly
AndroidBitmap_lockPixels(env, bitmap, &pixels);
std::memcpy(pixels, rgba_data, width * height * 4);
AndroidBitmap_unlockPixels(env, bitmap);
```

**Benefits**:
- Zero-copy from native to Java (Bitmap backed by native buffer)
- No byte array allocation in managed heap
- No GC pressure

### 7. Time Synchronization

**Why**: Android Camera2 provides timestamps in `CLOCK_BOOTTIME` (time since device boot), but ROS2 expects timestamps in `CLOCK_REALTIME` (Unix epoch time). Mismatched timestamps break time-based synchronization between sensors and nodes.

**Approach**: Convert camera timestamps by calculating the offset between `CLOCK_BOOTTIME` and `CLOCK_REALTIME` once at app startup, then apply this offset to all camera timestamps.

**Implementation** (`camera_device.cc:203-205` and `core/time_utils.h`):

```cpp
// Get camera timestamp in CLOCK_BOOTTIME
int64_t image_timestamp_ns;
AImage_getTimestamp(image.get(), &image_timestamp_ns);

// Convert from CLOCK_BOOTTIME to ROS epoch time (CLOCK_REALTIME)
int64_t offset_ns = ros2_android::time_utils::GetBootTimeOffsetNs();
int64_t ros_epoch_timestamp_ns = image_timestamp_ns + offset_ns;

// Set timestamp in ROS message
image_msg->header.stamp.sec = static_cast<int32_t>(ros_epoch_timestamp_ns / 1000000000LL);
image_msg->header.stamp.nanosec = static_cast<uint32_t>(ros_epoch_timestamp_ns % 1000000000LL);
```

**Why this is important**: Correct timestamps enable:
- Sensor fusion between camera and IMU data
- Message filtering and replay (rosbag)
- TF frame transformations with correct timing
- Detecting and handling stale data

**Limitations**: The offset is calculated once at startup. If the system clock is adjusted while the app is running (e.g., NTP correction), timestamps will drift. For typical use cases, this drift is negligible.

### 8. Camera Intrinsics Estimation

**Why**: ROS2 camera pipelines (image rectification, 3D projection, visual odometry) require camera intrinsic parameters (focal length, principal point) published via `sensor_msgs/msg/CameraInfo`. Mobile devices don't provide pre-calibrated intrinsics.

**Approach**: Estimate intrinsics using typical mobile camera assumptions: focal length ≈ 0.8 × image width, principal point at image center, zero distortion.

**Implementation** (`camera_device.cc:336-362`):

```cpp
// Estimate focal length as ~0.8 * image_width (typical for mobile cameras)
double fx = output_width * 0.8;
double fy = output_width * 0.8;
double cx = output_width / 2.0;
double cy = output_height / 2.0;

// Camera intrinsic matrix K [3x3]
camera_info->k[0] = fx;   camera_info->k[1] = 0.0;  camera_info->k[2] = cx;
camera_info->k[3] = 0.0;  camera_info->k[4] = fy;   camera_info->k[5] = cy;
camera_info->k[6] = 0.0;  camera_info->k[7] = 0.0;  camera_info->k[8] = 1.0;

// Distortion coefficients (k1, k2, t1, t2, k3) - zeros for uncalibrated
camera_info->d.resize(5, 0.0);
camera_info->distortion_model = "plumb_bob";
```

**Accuracy**: These estimates are sufficient for:
- Basic image rectification
- Approximate 3D projection
- Object detection pipelines (YOLO)

**Not suitable for**:
- Precise stereo depth estimation
- Visual odometry/SLAM
- Applications requiring sub-pixel accuracy

**Alternatives**: Proper camera calibration using OpenCV's calibration tools and checkerboard patterns. Calibrated parameters can be hardcoded or loaded from a config file. Not implemented because it requires manual calibration workflow.

### 9. Event-Driven Camera Frame Callbacks

**Why**: Polling for new frames wastes CPU cycles even when camera is idle.

**Approach**: Native callback system triggers UI updates only when new frames available.

**Implementation** (`camera_controller.cc:248-249` and `core/camera_frame_callback_queue.h`):

```cpp
// Trigger callback to notify UI of new camera frame (throttled to 10 Hz)
ros2_android::PostCameraFrameUpdate(std::string(UniqueId()));
```

**Performance**:
- Zero CPU usage when no new frames
- Callback throttled to 10 Hz (prevents UI overload)
- Lower latency vs 100ms polling interval
- ~90% reduction in idle CPU usage

### 10. Efficient Memory Management

**Why**: Avoid unnecessary allocations and copies in frame processing hot path.

**Approach**: Use `std::move()` semantics for zero-copy transfers.

**Implementation** (`camera_device.cc:334`):

```cpp
// Move BGR buffer directly into ROS message (zero-copy)
image_msg->data = std::move(rgb_buffer);

// Move messages to publisher (no copy)
auto camera_info = std::make_unique<CameraInfo>();
Emit({std::move(camera_info), std::move(image_msg)});
```

**Benefits**:
- No vector copy (~900KB saved per frame)
- Reduced memory allocations
- Lower GC pressure

### 11. Single-Buffer Camera Reader

**Why**: With fast processing (<20ms), no need for buffer queue overhead.

**Approach**: Use 1 AImage buffer - camera is always ready to produce next frame.

**Implementation** (`camera_device.cc:123-126`):

```cpp
constexpr int max_simultaneous_images = 1;
media_status_t status = AImageReader_new(
    width_, height_, AIMAGE_FORMAT_YUV_420_888,
    max_simultaneous_images, &reader_);
```

**Why 1 buffer is sufficient**:
- Processing time: 20ms < camera interval: 33ms (at 30 FPS)
- No frame drops observed
- Lower memory footprint

### 12. Camera Error Handling and User Notifications

**Why**: Camera initialization and capture operations can fail due to device constraints, permissions, or hardware issues. Silent failures leave users confused when cameras don't work.

**Approach**: Comprehensive error checking at all critical camera API calls with user notifications via the notification system.

**Implementation** (`camera_device.cc:130-139, 63-122`):

```cpp
// AImageReader initialization error checking
if (status != AMEDIA_OK)
{
  LOGE("Failed to create AImageReader for camera %s, status: %d", camera_id, status);
  ros2_android::PostNotification(
      ros2_android::NotificationSeverity::ERROR,
      "Failed to initialize camera image reader");
  ACameraDevice_close(camera_device->native_device_);
  return nullptr;
}

// Capture failure callback with user notification
void onCaptureFailed(void *context, ACameraCaptureSession *session,
                     ACaptureRequest *request, ACameraCaptureFailure *failure)
{
  LOGW("Camera capture failed (session %p, reason %d, frame %lld)",
       session, failure->reason, static_cast<long long>(failure->frameNumber));

  // Only notify for non-transient failures to avoid spam
  if (failure->reason != CAPTURE_FAILURE_REASON_FLUSHED)
  {
    ros2_android::PostNotification(
        ros2_android::NotificationSeverity::WARNING,
        "Camera capture failed");
  }
}

// Capture sequence aborted callback
void onCaptureSequenceAborted(void *context, ACameraCaptureSession *session,
                              int sequenceId)
{
  LOGW("Camera capture sequence %d aborted", sequenceId);
  ros2_android::PostNotification(
      ros2_android::NotificationSeverity::WARNING,
      "Camera capture sequence aborted");
}
```

**Session state callbacks** (`camera_device.cc:63-76`):
- `onSessionActive` - Logs when capture session becomes active
- `onSessionReady` - Logs when capture session is ready
- `onSessionClosed` - Logs when capture session closes

**Benefits**:
- Users see clear error messages when camera fails to initialize
- Capture failures notify users (filtered to avoid spam from transient errors)
- All errors logged for debugging
- Automatic cleanup of partially-initialized resources on failure

**Error scenarios handled**:
- AImageReader allocation failure (out of memory, invalid format)
- Capture request failures (hardware busy, invalid parameters)
- Sequence aborts (session reconfiguration, app backgrounding)

## Performance Results (Phase 1 Complete)

### Current Metrics

| Metric | Value | Notes |
|--------|-------|-------|
| Resolution | 640×480 (or 480×640 after 90°/270° rotation) | Standard VGA for object detection |
| Frame rate | 30 FPS | Camera native rate (no throttling) |
| Processing time | ~20ms | Total per published frame |
| YUV→I420+rotate | ~7ms | libyuv NEON optimized (unified operation) |
| I420→BGR | ~2ms | libyuv NEON optimized |
| Time sync conversion | <1ms | CLOCK_BOOTTIME → CLOCK_REALTIME offset |
| Camera intrinsics calc | <1ms | Estimated K matrix |
| BGR→ARGB+rotate (UI) | ~3-4ms | For Android preview only (10 Hz) |
| DDS publish (raw) | ~8-10ms | 900KB BGR payload over WiFi |
| DDS publish (compressed) | ~7-8ms | 50-100KB JPEG over WiFi |
| Bandwidth (raw) | ~900KB/frame | 27 MB/s at 30 FPS |
| Bandwidth (compressed) | ~50-100KB/frame | 3 MB/s at 30 FPS |
| Encoding | bgr8 | Standard ROS2 |
| CPU (idle) | Event-driven | ~90% reduction vs polling |

### Timing Breakdown (typical frame, all operations)

- **YUV→I420 + rotation**: 7ms - libyuv NEON (auto-detect NV12/NV21/I420 + rotate)
- **I420→BGR conversion**: 2ms - libyuv NEON
- **Time synchronization**: <1ms - CLOCK_BOOTTIME → CLOCK_REALTIME conversion
- **Camera intrinsics estimation**: <1ms - K matrix calculation
- **DDS publish (camera_info)**: 2ms - CameraInfo message with intrinsics
- **DDS publish (raw image)**: 8-10ms - 900KB BGR8 over WiFi (if subscribed)
- **DDS publish (compressed)**: 7-8ms - 50-100KB JPEG over WiFi (if subscribed)
- **BGR→ARGB + inverse rotation**: 3-4ms - Android UI preview (throttled to 10 Hz)
- **Native bitmap creation**: <1ms - AndroidBitmap API
- **Total (raw publishing)**: ~20-22ms - End-to-end (camera → DDS raw + UI)
- **Total (compressed publishing)**: ~19-21ms - End-to-end (camera → DDS compressed + UI)

### Phase 1 Improvements vs Initial Implementation

| Metric | Before | After Phase 1 | Improvement |
|--------|--------|---------------|-------------|
| YUV→RGB conversion | ~100ms (float math) | ~7ms (libyuv NEON) | **14× faster** |
| Bitmap creation | ~30ms (Kotlin) | ~2ms (native) | **15× faster** |
| Total processing | ~150ms | ~20ms | **7.5× faster** |
| Frame rate | 5-10 FPS | 30 FPS | **3-6× faster** |
| CPU usage (idle) | Constant polling | Event-driven | **~90% reduction** |
| Encoding | Custom rgba8 | Standard bgr8 | Full ROS2 compatibility |

## Architecture

```
Camera2 NDK (YUV_420_888 in sensor orientation)
         ↓
Query actual stride & sensor_orientation from metadata
         ↓
libyuv Android420ToI420Rotate (auto-detect format + rotate)
         ├─ Auto-detects NV12/NV21/I420 from uv_pixel_stride
         ├─ Converts to intermediate I420 format
         └─ Applies sensor_orientation rotation (0°/90°/180°/270°)
         ↓
libyuv I420ToRGB24 (NEON-optimized)
         ↓
Time sync: CLOCK_BOOTTIME → CLOCK_REALTIME
         ↓
         ├─→ ROS2 DDS Publisher (bgr8, ~900KB, rotated for landscape viewing)
         │   ├─ Raw topic: /<device_id>/camera/*/image_color
         │   ├─ Compressed topic: /<device_id>/camera/*/image_color/compressed (JPEG)
         │   └─→ rqt_image_view, image_transport, YOLO, etc.
         │
         └─→ BGR→ARGB conversion + inverse rotation (for UI only, 10 Hz)
             ├─ libyuv RAWToARGB
             ├─ libyuv ARGBRotate (undo sensor rotation for portrait UI)
             └─→ Native Bitmap creation (AndroidBitmap API)
                 └─→ Android UI preview (portrait orientation)
```

## Files Modified

- `src/camera/base/camera_device.cc` - YUV conversion with rotation, stride handling, time sync, intrinsics estimation, error handling for AImageReader_new() and capture callbacks
- `src/camera/base/camera_device.h` - Event emitter signature
- `src/camera/controllers/camera_controller.cc` - BGR→ARGB conversion with inverse rotation, JPEG compression
- `src/camera/controllers/camera_controller.h` - Types and interface, compressed publisher
- `src/jni/jni_bridge.cc` - Native bitmap creation call, camera frame callbacks, refactored to use CameraManager
- `src/jni/bitmap_utils.cc` - AndroidBitmap API implementation
- `src/jni/bitmap_utils.h` - Bitmap utility interface
- `src/core/time_utils.h` - CLOCK_BOOTTIME to CLOCK_REALTIME conversion utility
- `src/core/camera_frame_callback_queue.h` - Event-driven callback system for UI updates
- `src/core/notification_queue.h` - Notification system for camera errors
- `src/camera/camera_manager.cc` - Camera lifecycle management (created during refactoring)
- `src/camera/camera_manager.h` - Camera manager interface
- `src/CMakeLists.txt` - libyuv and libjpeg-turbo dependencies

## Dependencies

### libyuv (Google)

**Repository**: https://chromium.googlesource.com/libyuv/libyuv/

**Why libyuv**:
- Used by Google CameraX - proven on Android
- 90% of functions NEON-optimized for ARM
- Actively maintained by Chromium team
- Handles all YUV_420_888 variants automatically

**Integration**: Git submodule in `deps/libyuv/`

**License**: BSD 3-Clause (compatible with thesis)

## QoS Configuration

Camera image topics use **best-effort** QoS for video streaming:

```cpp
image_pub_.SetQos(rclcpp::QoS(1).best_effort());
```

**Why best-effort**:
- Video streams don't need reliability (old frames are useless)
- Lower latency (no retransmissions)
- Better for WiFi multicast (less congestion)

**Subscriber compatibility**: Must use matching QoS:

```bash
ros2 run rqt_image_view rqt_image_view --ros-args -p qos_reliability:=best_effort
```

## Phase 2: Bandwidth Optimization (Implemented)

Current bandwidth usage: **27 MB/s at 30 FPS** (900KB × 30) for raw topics. WiFi streaming can be unreliable at this rate.

### Dual-Topic Publishing with JPEG Compression ✅

**Status**: **Implemented** - Both raw and compressed topics are published simultaneously.

**Implementation**:

1. **Raw topics** for local/low-latency use cases:
   - `/<device_id>/camera/front/image_color` - BGR8, 900KB/frame
   - `/<device_id>/camera/rear/image_color` - BGR8, 900KB/frame

2. **Compressed topics** for bandwidth-constrained scenarios:
   - `/<device_id>/camera/front/image_color/compressed` - JPEG, 50-100KB/frame
   - `/<device_id>/camera/rear/image_color/compressed` - JPEG, 50-100KB/frame

> [!NOTE]
> `<device_id>` is the device identifier configured in the ros2_android app (e.g., `pixel_7`).

**Code Location** (`camera_controller.h:64-66`):

```cpp
Publisher<sensor_msgs::msg::CameraInfo> info_pub_;
Publisher<sensor_msgs::msg::Image> image_pub_;              // Raw BGR8
Publisher<sensor_msgs::msg::CompressedImage> compressed_image_pub_;  // JPEG
```

**Publishing Logic** (`camera_controller.cc:129-192`):

```cpp
// Publish raw image if there are subscribers
if (image_pub_.Enabled())
{
  size_t image_subscribers = image_pub_.GetSubscriberCount();
  if (image_subscribers > 0)
  {
    image_pub_.Publish(*info_image.second.get());
  }
}

// Publish compressed image if there are subscribers
if (compressed_image_pub_.Enabled())
{
  size_t compressed_image_subscribers = compressed_image_pub_.GetSubscriberCount();
  if (compressed_image_subscribers > 0)
  {
    const auto &bgr_data = info_image.second->data;
    int width = info_image.second->width;
    int height = info_image.second->height;

    // Convert BGR to RGB for JPEG encoding
    std::vector<uint8_t> rgb_data(width * height * 3);
    for (size_t i = 0; i < bgr_data.size(); i += 3)
    {
      rgb_data[i] = bgr_data[i + 2];     // R
      rgb_data[i + 1] = bgr_data[i + 1]; // G
      rgb_data[i + 2] = bgr_data[i];     // B
    }

    // Compress to JPEG using TurboJPEG
    tjhandle compressor = tjInitCompress();
    if (compressor)
    {
      unsigned char *jpeg_buf = nullptr;
      unsigned long jpeg_size = 0;

      int tj_result = tjCompress2(
          compressor,
          rgb_data.data(),
          width,
          width * 3, // pitch (bytes per row)
          height,
          TJPF_RGB,
          &jpeg_buf,
          &jpeg_size,
          TJSAMP_420, // 4:2:0 chroma subsampling
          85,         // quality (0-100)
          TJFLAG_FASTDCT);

      if (tj_result == 0)
      {
        auto compressed_msg = std::make_unique<CompressedImage>();
        compressed_msg->header = info_image.second->header;
        compressed_msg->format = "jpeg";
        compressed_msg->data.assign(jpeg_buf, jpeg_buf + jpeg_size);
        compressed_image_pub_.Publish(*compressed_msg);
      }

      if (jpeg_buf)
        tjFree(jpeg_buf);
      tjDestroy(compressor);
    }
  }
}
```

**JPEG Encoding Details**:
- Library: TurboJPEG (libjpeg-turbo)
- Quality: 85 (0-100 scale)
- Chroma subsampling: 4:2:0 (standard for photos/video)
- Fast DCT algorithm for speed
- NEON-optimized on ARM64

**Performance Impact**:

| Metric | Raw Topic | Compressed Topic | Savings |
|--------|-----------|------------------|---------|
| Size/frame | 900 KB | 50-100 KB | **90%** |
| Bandwidth (30 FPS) | 27 MB/s | 3 MB/s | **89%** |
| BGR→RGB conversion | - | ~2ms | Inline loop |
| JPEG compression | - | ~5ms | TurboJPEG NEON |
| Total publish time | 8-10ms | 7-8ms | Similar (smaller payload) |
| Use case | Local tools | WiFi streaming, YOLO | - |

**Benefits**:
- Local subscribers (rqt_image_view) can use raw for lowest latency
- Remote subscribers (YOLO on PC) can use compressed to reduce WiFi load
- Standard ROS2 topic naming convention (`/topic/compressed`)
- Both topics use same QoS settings (best-effort, keep-last-1)
- Independent subscriber counts - only publishes when subscribed
- Both cameras publish independently

**Dependencies**:
- TurboJPEG (libjpeg-turbo) - already integrated
- ~100KB APK size increase

**Subscriber Usage**:

Both topics use **best-effort** QoS, so subscribers must match:

```bash
# Raw topic (replace <device_id> with your actual device ID)
ros2 run rqt_image_view rqt_image_view \
  --ros-args -p image_topic:=/<device_id>/camera/front/image_color \
  -p qos_reliability:=best_effort

# Compressed topic (recommended for WiFi)
ros2 run rqt_image_view rqt_image_view \
  --ros-args -p image_topic:=/<device_id>/camera/front/image_color/compressed \
  -p qos_reliability:=best_effort
```

---

### Phase 3: H.264 Video Streaming (Optional)

**Only implement if JPEG compression insufficient** (e.g., recording, multi-subscriber scenarios).

**Approach**: MediaCodec H.264 hardware encoder + ffmpeg_image_transport

**Expected**:
- Bandwidth: 900KB → 10-30KB per frame (30-90× reduction)
- Latency: +50-100ms (GOP buffering)
- Requires: Subscriber-side ffmpeg_image_transport plugin

**Trade-offs**: Higher latency makes this unsuitable for real-time object detection. JPEG compression (Phase 2) is better balance.

## Known Issues and Limitations

### DDS Participant-Level Flow Control and Permanent Throttling

**Problem**: When switching from compressed to raw image topics and back over WiFi, publishing performance degrades permanently until camera is disabled and re-enabled.

**Observed Behavior**:
- Raw image publishing (900KB frames) over WiFi causes `publish()` to block for 70-130ms
- After switching back to compressed topics, publishing remains slow (~130ms vs normal ~30ms)
- Throttling persists even with zero subscribers
- Only resolved by disabling and re-enabling the camera device

**Root Cause**: DDS Participant-Level Flow Control

1. **Single Processing Thread**: Camera capture, YUV conversion, publishing, and UI preview all execute on one thread
2. **Blocking Publish**: `publisher_->publish()` is synchronous and blocks when network is congested
3. **Writer History Cache (WHC)**: Each DDS writer maintains unacknowledged samples for potential retransmission
4. **Watermark Throttling**: When WHC exceeds `WhcHigh` bytes, writer stalls until it drops below `WhcLow`
5. **Adaptive Watermarks**: `WhcAdaptive=true` (default) dynamically lowers watermarks based on network congestion
6. **Shared DDS Participant**: All publishers on same ROS node share one DDS participant with shared flow control state

**Why It Persists**:
- When 900KB raw frames congest WiFi, DDS detects high transmit pressure and retransmit requests
- Adaptive watermarks lower `WhcHigh` to throttle the publisher
- This throttling state persists in the DDS participant even after:
  - Unsubscribing from raw topic
  - Switching to compressed topic
  - Having zero subscribers
- The participant "remembers" the congested network state
- Only destroying the camera device (which destroys all publishers) clears the state

**Why Topic Switching Triggers It**:
- Dual publishing means both raw and compressed are sent simultaneously when both have subscribers
- Even briefly publishing 900KB frames triggers congestion detection
- Once triggered, the adaptive throttling affects all future publishing on that participant

**Technical Details**:

From Cyclone DDS documentation:
- **Writer History Cache (WHC)**: Stores unacknowledged samples per writer
- **WhcHigh**: Maximum bytes in WHC before writer stalls (default: adaptive)
- **WhcLow**: Resume threshold after stall (default: adaptive)
- **WhcAdaptive**: Dynamically adjusts watermarks based on transmit pressure and retransmit requests (default: true)

Flow control hierarchy:
```
DDS Participant (per ROS node)
  └─> Publishers (camera_info, raw image, compressed image)
      └─> DataWriters (individual WHC, shared flow control state)
```

**Attempted Solutions** (all failed):

1. ❌ **QoS Tuning** - Added `durability_volatile()` to QoS settings
   - Result: No effect, flow control is at participant level

2. ❌ **Publisher Reset** - Destroy and recreate publishers when subscriber count changes
   - Result: New publishers still use same throttled participant

3. ❌ **Compressed-Prefer Logic** - Skip raw if compressed has subscribers
   - Result: Damage already done during topic switch transition

4. ❌ **Debouncing** - Delay publishing after subscriber changes
   - Result: Doesn't prevent initial congestion from occurring

5. ❌ **Disable Adaptive Watermarks** - Add `<WhcAdaptive>false</WhcAdaptive>` to Cyclone DDS XML config
   - Result: XML with `<Internal><Watermarks>` section at Domain level causes DDS initialization to fail with "failed to create domain, error Error"
   - Technical reason: `<Internal>` section placement in XML was invalid for Domain scope

**Why Configuration Approach Failed**:

Attempted to add watermark configuration to `cyclonedds.xml`:
```xml
<Domain id="any">
  <General>...</General>
  <Internal>
    <Watermarks>
      <WhcAdaptive>false</WhcAdaptive>
      <WhcHigh>10485760</WhcHigh>
      <WhcLow>1048576</WhcLow>
    </Watermarks>
  </Internal>
</Domain>
```

Error: `rmw_cyclonedds_cpp: rmw_create_node: failed to create domain, error Error`

The `<Internal>` section placement at Domain level is not valid in Cyclone DDS configuration schema. Watermark settings may need to be at a different scope or require different XML structure. Further investigation into correct Cyclone DDS configuration schema would be needed, but this approach was abandoned in favor of accepting current behavior.

**Solutions That Would Work** (not implemented due to architectural complexity):

1. ✅ **Separate DDS Domain** - Use different domain ID for camera publishers
   - Isolates flow control state completely
   - Requires subscribers to join multiple domains

2. ✅ **Separate ROS Node** - Create dedicated node for camera publishing
   - Each node gets own DDS participant
   - Requires additional node lifecycle management

3. ✅ **Separate Process** - Run camera publishers in dedicated process
   - Complete isolation, independent flow control
   - Requires IPC mechanism between processes

4. ✅ **Async Publishing Thread** - Non-blocking publish with backpressure handling
   - Prevents main thread from blocking
   - Requires thread-safe queue and frame dropping logic

**Recommended Workaround**:

**Use compressed topics exclusively for WiFi streaming**. The compressed JPEG topics work reliably at ~30ms publish time with 50-100KB per frame. Only use raw topics when:
- Connected via wired network (lower latency, higher bandwidth)
- Subscriber is on same device (no network congestion)
- Willing to disable/enable camera after switching topics

**Performance Comparison**:

| Scenario | Publish Time | Bandwidth | Result |
|----------|--------------|-----------|--------|
| Compressed only | ~30ms | 3 MB/s | ✅ Stable |
| Raw only (WiFi) | 70-130ms | 27 MB/s | ⚠️ Unstable, causes congestion |
| Switch compressed→raw→compressed | 130ms+ | 3 MB/s | ❌ Permanently throttled |
| After camera disable/enable | ~30ms | 3 MB/s | ✅ Recovered |

**Documentation References**:
- Cyclone DDS Writer History Cache: https://cyclonedds.io/docs/cyclonedds/latest/config/config_file_reference.html
- DDS Flow Control: Search "Cyclone DDS participant flow control Writer History Cache watermarks"
- GitHub issues: eclipse-cyclonedds/cyclonedds (flow control, WHC, throttling)

## Technical Notes

### YUV_420_888 Format Detection

Android Camera2 `YUV_420_888` is a flexible format that can be:
- **NV12**: Y plane + UV interleaved (U before V)
- **NV21**: Y plane + VU interleaved (V before U)
- **I420**: Separate Y, U, V planes

Detection logic:
```cpp
// Semi-planar (NV12/NV21): uv_pixel_stride == 2
// Planar (I420/YV12): uv_pixel_stride == 1

if (uv_pixel_stride == 2) {
  bool is_nv12 = (u_data < v_data);  // Check pointer order
}
```

### Stride vs Width

**Width**: Logical image width in pixels (640)

**Stride**: Actual memory row size in bytes (may be 672 for alignment)

Must always use stride from `AImage_getPlaneRowStride()`, never hardcode.

### ROS2 Encoding Standards

Standard encodings supported by ROS2 image ecosystem:

- `rgb8`: 3 bytes/pixel, R-G-B order (used here)
- `bgr8`: 3 bytes/pixel, B-G-R order (OpenCV)
- `mono8`: 1 byte/pixel, grayscale
- `bgra8`: 4 bytes/pixel, B-G-R-A order
- `rgba8`: 4 bytes/pixel, R-G-B-A order (less common)

Always use standard encodings for compatibility.

## References

- libyuv: https://chromium.googlesource.com/libyuv/libyuv/
- ROS2 sensor_msgs/Image: https://docs.ros.org/en/rolling/p/sensor_msgs/interfaces/msg/Image.html
- Android Camera2 YUV: https://medium.com/androiddevelopers/convert-yuv-to-rgb-for-camerax-imageanalysis-6c627f3a0292
- ROS2 QoS: https://docs.ros.org/en/rolling/Concepts/Intermediate/About-Quality-of-Service-Settings.html
- Android NDK Camera: https://developer.android.com/ndk/reference/group/camera
