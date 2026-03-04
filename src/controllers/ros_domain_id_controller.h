#pragma once

#include "controller.h"
#include "events.h"

#include <string>
#include <vector>

namespace sensors_for_ros {

constexpr const char* kRosDomainIdControllerId = "ros_domain_id_controller";

class RosDomainIdController : public Controller,
                              public event::Emitter<event::RosDomainIdChanged> {
 public:
  RosDomainIdController();
  virtual ~RosDomainIdController(){};

  void SetNetworkInterfaces(std::vector<std::string> interfaces);

  void DrawFrame() override;

  std::string PrettyName() const override { return "ROS Domain ID"; }

 private:
  std::string preferred_interface_;
  std::vector<std::string> network_interfaces_;
};
}  // namespace sensors_for_ros
