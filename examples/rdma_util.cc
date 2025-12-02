#include "rdma_util.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include <infiniband/verbs.h>

namespace RDMA_EC {

enum ibv_qp_type DEFAULT_RDMA_TRANSPORT = IBV_QPT_RC;

std::string Config::trim(const std::string &str) const {
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

bool Config::parse_line(const std::string &line) {
  std::string trimmed = trim(line);

  // Skip empty lines and comments
  if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
    return true;
  }

  // Parse key=value format
  size_t eq_pos = trimmed.find('=');
  if (eq_pos == std::string::npos) {
    return false;
  }

  std::string key = trim(trimmed.substr(0, eq_pos));
  std::string value = trim(trimmed.substr(eq_pos + 1));

  // Strip quotes from value if present
  if (value.size() >= 2 && 
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }

  if (key.empty()) {
    return false;
  }

  // Parse and set values
  try {
    if (key == "mtu") {
      mtu = std::stoull(value);
    } else if (key == "chunk_size") {
      chunk_size = std::stoull(value);
    } else if (key == "buffer_size") {
      buffer_size = std::stoull(value);
    } else if (key == "cpu_core_id") {
      cpu_core_id = std::stoi(value);
    } else if (key == "receiver_timeout_seconds") {
      receiver_timeout_seconds = std::stoi(value);
    } else if (key == "transport_type") {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (value == "rc") {
        transport_type = IBV_QPT_RC;
      } else if (value == "uc") {
        transport_type = IBV_QPT_UC;
      } else {
        std::cerr << "[Config] Warning: Unknown transport_type value: " << value 
                  << " (expected 'rc' or 'uc'). Using default." << std::endl;
      }
    } else if (key == "enable_logging") {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (value == "true" || value == "1" || value == "yes") {
        enable_logging = true;
      } else if (value == "false" || value == "0" || value == "no") {
        enable_logging = false;
      }
    } else {
      std::cerr << "[Config] Unknown Config key: " << key << std::endl;
      return true;
    }
  } catch (const std::exception &e) {
    std::cerr << "[Config] Warning: Failed to parse value for " << key << ": "
              << value << " (" << e.what() << ")" << std::endl;
    return false;
  }

  return true;
}

bool Config::load_from_file(const std::string &filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "[Config] Failed to open config file: " << filepath
              << std::endl;
    return false;
  }

  std::string line;
  int line_num = 0;
  bool has_errors = false;

  while (std::getline(file, line)) {
    line_num++;
    if (!parse_line(line)) {
      std::cerr << "[Config] Warning: Invalid line " << line_num
                << " in config file: " << line << std::endl;
      has_errors = true;
    }
  }

  file.close();

  // Config loading messages use std::cout directly (before logger is initialized)
  std::cout << "[Config] Loaded configuration from " << filepath << std::endl;
  if (has_errors) {
    std::cerr << "[Config] Some errors occurred while parsing config file"
              << std::endl;
  }

  return true;
}

bool Config::save_to_file(const std::string &filepath) const {
  std::ofstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "[Config] Failed to create config file: " << filepath
              << std::endl;
    return false;
  }

  file << "# RDMA Configuration File\n";
  file << "# Generated automatically\n\n";
  file << "mtu=" << mtu << "\n";
  file << "chunk_size=" << chunk_size << "\n";
  file << "buffer_size=" << buffer_size << "\n";
  file << "cpu_core_id=" << cpu_core_id << "\n";
  file << "receiver_timeout_seconds=" << receiver_timeout_seconds << "\n";
  file << "enable_logging=" << (enable_logging ? "true" : "false") << "\n";

  file.close();

  // Config saving messages use std::cout directly
  std::cout << "[Config] Saved configuration to " << filepath << std::endl;
  return true;
}

void Config::print() const {
  std::cout << "[Config] Current configuration:" << std::endl;
  std::cout << "  mtu = " << mtu << std::endl;
  std::cout << "  chunk_size = " << chunk_size << std::endl;
  std::cout << "  buffer_size = " << buffer_size << std::endl;
  std::cout << "  cpu_core_id = " << cpu_core_id << std::endl;
  std::cout << "  receiver_timeout_seconds = " << receiver_timeout_seconds
            << std::endl;
  std::cout << "  enable_logging = " << (enable_logging ? "true" : "false") << std::endl;
}

} // namespace RDMA_EC
