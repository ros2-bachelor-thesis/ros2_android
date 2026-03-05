#pragma once

#include <mutex>
#include <string>

namespace ros2_android {

class SensorDataProvider {
 public:
  SensorDataProvider(const std::string& unique_id) : unique_id_(unique_id) {}
  virtual ~SensorDataProvider() = default;

  virtual std::string PrettyName() const = 0;
  virtual std::string GetLastMeasurementJson() = 0;

  virtual const char* SensorName() const { return ""; }
  virtual const char* SensorVendor() const { return ""; }
  virtual const char* TopicName() const { return ""; }
  virtual const char* TopicType() const { return ""; }

  const char* UniqueId() const { return unique_id_.c_str(); }

  virtual void Enable() = 0;
  virtual void Disable() = 0;
  virtual bool IsEnabled() const = 0;

 private:
  const std::string unique_id_;
};

}  // namespace ros2_android
