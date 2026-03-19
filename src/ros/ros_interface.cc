#include "ros/ros_interface.h"

#include "core/log.h"

using ros2_android::RosInterface;

RosInterface::RosInterface() : device_id_("android") {}

RosInterface::RosInterface(const std::string& device_id)
    : device_id_(device_id.empty() ? "android" : device_id) {}

void RosInterface::Initialize(size_t ros_domain_id) {

  rclcpp::InitOptions init_options;
  init_options.set_domain_id(ros_domain_id);
  init_options.shutdown_on_signal = false;
  context_ = std::make_shared<rclcpp::Context>();
  context_->init(0, nullptr, init_options);

  rclcpp::NodeOptions node_options;
  node_options.context(context_);
  std::string node_name = device_id_ + "_node";
  node_ = std::make_shared<rclcpp::Node>(node_name, node_options);

  rclcpp::ExecutorOptions executor_options;
  executor_options.context = context_;
  executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>(
      executor_options);
  executor_->add_node(node_);

  executor_thread_ = std::thread(&rclcpp::Executor::spin, executor_.get());

  NotifyInitChanged();
}

void RosInterface::Shutdown() {
  // Signal shutdown to ROS context
  context_->shutdown("RosInterface asked to Shutdown");

  // Wait for executor thread to finish
  executor_thread_.join();

  // Clear observers without notifying them - publishers already disabled in jni_bridge
  // Notifying would cause double-free since Disable() already called DestroyPublisher()
  observers_.clear();

  // Cleanup resources
  node_.reset();
  executor_.reset();
  context_.reset();
}

bool RosInterface::Initialized() const {
  return context_ && context_->is_valid();
}

rclcpp::Context::SharedPtr RosInterface::get_context() const {
  return context_;
}

rclcpp::Node::SharedPtr RosInterface::get_node() const { return node_; }

const std::string& RosInterface::GetDeviceId() const { return device_id_; }

void RosInterface::AddObserver(std::function<void(void)> init_or_shutdown) {
  observers_.push_back(init_or_shutdown);
}

void RosInterface::NotifyInitChanged() {
  for (auto& observer : observers_) {
    LOGI("Notifying observer %p", &observer);
    observer();
  }
  // Still want observations? ask for them again
  observers_.clear();
}
