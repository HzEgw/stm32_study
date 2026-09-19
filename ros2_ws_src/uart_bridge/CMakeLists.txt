cmake_minimum_required(VERSION 3.8)
project(uart_bridge)

# ---- C++ 标准 ----
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- 找依赖 ----
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

# ---- 可执行文件 ----
add_executable(uart_bridge_node src/uart_bridge_node.cpp)
ament_target_dependencies(uart_bridge_node
  rclcpp
  std_msgs
)

# ---- 安装 ----
install(TARGETS uart_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
