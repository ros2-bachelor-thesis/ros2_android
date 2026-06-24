#include "core/network_manager.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/log.h"

using ros2_android::NetworkManager;

NetworkManager::NetworkManager() {}

NetworkManager::~NetworkManager() {}

void NetworkManager::SetNetworkInterfaces(
    const std::vector<std::string> &interfaces)
{
  network_interfaces_ = interfaces;
  LOGI("NetworkManager: Set %zu network interfaces", interfaces.size());
}

const std::vector<std::string> &NetworkManager::GetNetworkInterfaces() const
{
  return network_interfaces_;
}

bool NetworkManager::GenerateCycloneDdsConfig(
    const std::string &cache_dir, int32_t ros_domain_id,
    const std::string &network_interface)
{
  // Build config file path
  std::string cyclone_uri = cache_dir;
  if (cyclone_uri.back() != '/')
  {
    cyclone_uri += '/';
  }
  cyclone_uri += "cyclonedds.xml";

  // Check if configuration has changed
  if (!HasConfigChanged(cache_dir, ros_domain_id, network_interface))
  {
    LOGI("NetworkManager: Cyclone DDS config unchanged, reusing existing file");
    return true;
  }

  LOGI("NetworkManager: Generating Cyclone DDS config at %s", cyclone_uri.c_str());
  LOGI("  Domain ID: %d, Interface: %s", ros_domain_id, network_interface.c_str());

  // Write the configuration file
  if (!WriteConfigFile(cyclone_uri, network_interface))
  {
    LOGE("NetworkManager: Failed to write Cyclone DDS config");
    return false;
  }

  // Update cached state
  last_cache_dir_ = cache_dir;
  last_domain_id_ = ros_domain_id;
  last_network_interface_ = network_interface;
  config_path_ = cyclone_uri;

  // Set environment variables for ROS 2 / Cyclone DDS
  setenv("CYCLONEDDS_URI", cyclone_uri.c_str(), 1);
  setenv("ROS_DOMAIN_ID", std::to_string(ros_domain_id).c_str(), 1);
  setenv("RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp", 1);
  // Force the micro-ROS agent's FastDDS middleware to use the same DDS domain as the
  // rest of the app. Without this, the ESP32 firmware's hardcoded domain 0 causes the
  // agent's DDS entities to be invisible when ROS_DOMAIN_ID != 0.
  setenv("XRCE_DOMAIN_ID_OVERRIDE", std::to_string(ros_domain_id).c_str(), 1);
  LOGI("NetworkManager: Cyclone DDS config generated successfully");
  return true;
}

std::string NetworkManager::GetCycloneDdsConfigPath() const
{
  return config_path_;
}

bool NetworkManager::HasConfigChanged(
    const std::string &cache_dir, int32_t ros_domain_id,
    const std::string &network_interface) const
{
  return (cache_dir != last_cache_dir_ ||
          ros_domain_id != last_domain_id_ ||
          network_interface != last_network_interface_);
}

bool NetworkManager::WriteConfigFile(
    const std::string &path, const std::string &network_interface) const
{
  std::ofstream config_file(path, std::ofstream::trunc);
  if (!config_file.is_open())
  {
    LOGE("NetworkManager: Failed to open config file at %s", path.c_str());
    return false;
  }

  // Write Cyclone DDS XML configuration
  // Enable multicast for DDS discovery on Android Wi-Fi
  // Small MaxMessageSize forces RTPS-level fragmentation (no IP fragmentation)
  // Large buffers/WHC needed for 8MB depth + 33MB PointCloud2 over WiFi
  config_file << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
  config_file << "<CycloneDDS xmlns=\"https://cdds.io/config\">\n";
  config_file << "  <Domain id=\"any\">\n";
  config_file << "    <General>\n";
  config_file << "      <AllowMulticast>spdp</AllowMulticast>\n";
  config_file << "      <MaxMessageSize>1472B</MaxMessageSize>\n";
  config_file << "      <FragmentSize>1344B</FragmentSize>\n";
  config_file << "      <Interfaces>\n";
  config_file << "        <NetworkInterface name=\"" << network_interface
              << "\"/>\n";
  config_file << "      </Interfaces>\n";
  config_file << "    </General>\n";
  config_file << "    <Internal>\n";
  config_file << "      <Watermarks>\n";
  config_file << "        <WhcLow>1MB</WhcLow>\n";
  config_file << "        <WhcHighInit>1MB</WhcHighInit>\n";
  config_file << "        <WhcHigh>16MB</WhcHigh>\n";
  config_file << "      </Watermarks>\n";
  config_file << "      <SocketReceiveBufferSize/>\n";
  config_file << "      <MaxQueuedRexmitBytes>4MB</MaxQueuedRexmitBytes>\n";
  config_file << "      <DefragReliableMaxSamples>16</DefragReliableMaxSamples>\n";
  config_file << "      <DefragUnreliableMaxSamples>4</DefragUnreliableMaxSamples>\n";
  config_file << "    </Internal>\n";
  config_file << "  </Domain>\n";
  config_file << "</CycloneDDS>\n";

  config_file.close();

  if (config_file.fail())
  {
    LOGE("NetworkManager: Failed to write config file");
    return false;
  }

  return true;
}
