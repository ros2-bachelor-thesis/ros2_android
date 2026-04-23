#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/point.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <vermin_collector_ros_msgs/msg/command.hpp>
#include <vermin_collector_ros_msgs/msg/feedback.hpp>

#include "ros/ros_interface.h"
#include "sensors/base/sensor_data_provider.h"

namespace ros2_android {

enum class TargetManagerState {
  INIT,
  CALIBRATING,
  READY,
  FIXED_POSITION_MODE,
  SENT_TARGET,
  WAITING_TO_RETURN,
  RETURNING,
  FINISHED
};

const char* TargetManagerStateName(TargetManagerState state);

class TargetManagerController : public SensorDataProvider {
 public:
  TargetManagerController(RosInterface& ros);
  ~TargetManagerController() override;

  // SensorDataProvider interface
  std::string PrettyName() const override;
  std::string GetLastMeasurementJson() override;
  bool GetLastMeasurement(jni::SensorReadingData& out_data) override;

  void Enable() override;
  void Disable() override;
  bool IsEnabled() const override { return enabled_; }

  // JNI-callable controls
  void SetFixedPositionMode(bool enabled);
  bool IsFixedPositionMode() const;
  TargetManagerState GetState() const;

 private:
  // Subscription callbacks (run on executor thread - no mutual exclusion needed)
  void OnTarget(const geometry_msgs::msg::Point::SharedPtr msg);
  void OnImu(const sensor_msgs::msg::Imu::SharedPtr msg);
  void OnFeedback(const vermin_collector_ros_msgs::msg::Feedback::SharedPtr msg);
  void OnFixedPosition(const std_msgs::msg::Float32MultiArray::SharedPtr msg);

  // State machine (100ms timer)
  void StateMachineCallback();
  bool Calibrate();

  // Targeting
  void SendTarget(const geometry_msgs::msg::Point& msg);
  void ReturnToZero();
  std::pair<float, float> ComputePanTiltDegrees(float x, float y, float z);
  void ClampPanTiltAngles(float& pan, float& tilt);

  // ROS infrastructure
  RosInterface& ros_;
  bool enabled_ = false;

  Publisher<vermin_collector_ros_msgs::msg::Command> command_pub_;

  rclcpp::Subscription<geometry_msgs::msg::Point>::SharedPtr target_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
  rclcpp::Subscription<vermin_collector_ros_msgs::msg::Feedback>::SharedPtr feedback_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr fixed_pos_sub_;

  rclcpp::TimerBase::SharedPtr timer_;

  // State (protected by state_mutex_ for JNI access)
  mutable std::mutex state_mutex_;
  TargetManagerState state_ = TargetManagerState::INIT;
  uint8_t esp32_state_ = 0;  // Feedback::READY
  bool fixed_position_mode_ = false;

  // Calibration
  float tilt_offset_ = 0.0f;
  float pan_offset_ = 0.0f;
  std::optional<std::pair<float, float>> imu_orientation_;

  // Last target tracking
  std::array<float, 2> last_fixed_position_ = {0.0f, 0.0f};
};

}  // namespace ros2_android
