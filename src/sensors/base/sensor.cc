#include "sensors/base/sensor.h"

#include <cassert>

#include "core/log.h"

using ros2_android::Sensor;

void Sensor::EventLoop()
{
  const int kSensorIdent = 42;

  looper_ = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
  ASensorEventQueue *queue = ASensorManager_createEventQueue(
      manager_, looper_, kSensorIdent, nullptr, nullptr);

  if (0 != ASensorEventQueue_requestAdditionalInfoEvents(queue, true))
  {
    LOGW("Couldn't enable additional info events");
  }
  ASensorEventQueue_enableSensor(queue, descriptor_.sensor_ref);

  int ident;
  int events;
  void *data;
  while (not shutdown_.load())
  {
    ident = ALooper_pollAll(-1, nullptr, &events, &data);

    if (ident == ALOOPER_POLL_ERROR)
    {
      LOGW("ALooper_pollAll error, shutting down");
      shutdown_.store(true);
      break;
    }

    if (ident != kSensorIdent)
    {
      continue;
    }

    ASensorEvent event;
    while (ASensorEventQueue_getEvents(queue, &event, 1) > 0)
    {
      if (event.sensor != descriptor_.handle)
      {
        continue;
      }

      if (event.type == ASENSOR_TYPE_ADDITIONAL_INFO)
      {
        if (event.additional_info.type == ASENSOR_ADDITIONAL_INFO_SENSOR_PLACEMENT)
        {
          // TODO(ohagenauer) store this position somewhere for correct positioning of attached devices
        }
        continue;
      }

      OnEvent(event);
    }
  }

  ASensorEventQueue_disableSensor(queue, descriptor_.sensor_ref);
  ASensorManager_destroyEventQueue(manager_, queue);
  ALooper_release(looper_);
}

void Sensor::Initialize()
{
  shutdown_.store(false);
  queue_thread_ = std::thread(&Sensor::EventLoop, this);
}

void Sensor::Shutdown()
{
  shutdown_.store(true);
  if (queue_thread_.joinable())
  {
    if (looper_ != nullptr)
    {
      ALooper_wake(looper_);
    }
    else
    {
      LOGW("Looper is null during shutdown, thread may not wake immediately");
    }
    queue_thread_.join();
  }
}
