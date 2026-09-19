// ============================================================================
//  uart_bridge_node.cpp —— STM32 串口帧协议 <-> ROS2 桥 (C++ / rclcpp)
//
//  对应 STM32 工程: 04_UART_Bluetooth(_Reg)
//  帧格式: 0xAA 0x55 | LEN(1B) | PAYLOAD[LEN] | SUM(1B),  SUM = (LEN + ΣPAYLOAD) & 0xFF
//
//  功能:
//    1) 轮询读串口 -> 解析帧 -> 发布
//         /mcu/frame   (std_msgs/UInt8MultiArray)  原始 payload
//         /mcu/counter (std_msgs/UInt16)           payload 前两字节(大端)当作计数器
//    2) 订阅 /mcu/tx (std_msgs/UInt8MultiArray) -> 打包成帧发给 STM32(调试/控制用)
//  参数(ROS2 parameter): port(默认 /dev/ttyUSB0), baud(默认 115200), publish_counter(bool)
//
//  编译/运行见本目录 README.md
// ============================================================================

#include <fcntl.h>        // open(), O_RDWR ...
#include <termios.h>      // termios, cfsetispeed ...
#include <unistd.h>       // read(), write(), close()
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"
#include "std_msgs/msg/u_int16.hpp"

namespace
{
constexpr uint8_t kHead0 = 0xAA;
constexpr uint8_t kHead1 = 0x55;
constexpr size_t  kMaxPayload = 32;
}  // namespace

class UartBridge : public rclcpp::Node
{
public:
  UartBridge() : rclcpp::Node("uart_bridge")
  {
    // ---------- 参数 ----------
    port_  = this->declare_parameter<std::string>("port", "/dev/ttyUSB0");
    baud_  = this->declare_parameter<int>("baud", 115200);
    publish_counter_ = this->declare_parameter<bool>("publish_counter", true);

    // ---------- 发布/订阅 ----------
    pub_frame_   = this->create_publisher<std_msgs::msg::UInt8MultiArray>("/mcu/frame", 10);
    pub_counter_ = this->create_publisher<std_msgs::msg::UInt16>("/mcu/counter", 10);
    sub_tx_      = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
      "/mcu/tx", 10, std::bind(&UartBridge::onTx, this, std::placeholders::_1));

    // ---------- 打开串口 ----------
    openPort();

    // ---------- 5ms 定时器: 非阻塞读 + 解析(避免另开线程) ----------
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(5), std::bind(&UartBridge::onTimer, this));

    RCLCPP_INFO(this->get_logger(), "uart_bridge started  port=%s baud=%d",
      port_.c_str(), baud_);
  }

private:
  // ======================= 串口: 打开与配置 =======================
  void openPort()
  {
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
      RCLCPP_ERROR(this->get_logger(), "打开串口失败: %s (%s) — 检查设备名/权限(dialout)",
        port_.c_str(), std::strerror(errno));
      return;
    }

    termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
      RCLCPP_ERROR(this->get_logger(), "tcgetattr 失败: %s", std::strerror(errno));
      ::close(fd_);
      fd_ = -1;
      return;
    }

    // 原始模式: 8 位数据, 1 位停止, 无校验, 无流控
    ::cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN]  = 0;      // 非阻塞: 没有数据就返回 0
    tty.c_cc[VTIME] = 0;

    speed_t sp = toSpeed(baud_);
    ::cfsetispeed(&tty, sp);
    ::cfsetospeed(&tty, sp);
    ::tcflush(fd_, TCIOFLUSH);

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
      RCLCPP_ERROR(this->get_logger(), "tcsetattr 失败: %s", std::strerror(errno));
      ::close(fd_);
      fd_ = -1;
      return;
    }
    RCLCPP_INFO(this->get_logger(), "串口已打开: %s @ %d 8N1", port_.c_str(), baud_);
  }

  static speed_t toSpeed(int baud)
  {
    switch (baud) {
      case 9600:   return B9600;
      case 57600:  return B57600;
      case 115200: return B115200;
      case 230400: return B230400;
      case 460800: return B460800;
      default:     return B115200;
    }
  }

  // ======================= 读: 5ms 轮询 + 帧解析 =======================
  void onTimer()
  {
    if (fd_ < 0) {          // 打不开就每秒重试一次(插拔 USB 场景)
      if (++retry_ >= 200) { retry_ = 0; openPort(); }
      return;
    }

    uint8_t buf[256];
    ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) { return; }

    for (ssize_t i = 0; i < n; ++i) { feedParser(buf[i]); }
  }

  void feedParser(uint8_t b)
  {
    switch (state_) {
      case 0: if (b == kHead0) { state_ = 1; } break;
      case 1:
        if (b == kHead1)      { state_ = 2; }
        else if (b == kHead0) { state_ = 1; }
        else                  { state_ = 0; }
        break;
      case 2:                       // LEN
        if (b == 0 || b > kMaxPayload) { state_ = 0; }
        else { need_ = b; idx_ = 0; sum_ = b; state_ = 3; }
        break;
      case 3:                       // PAYLOAD
        payload_[idx_++] = b;
        sum_ += b;
        if (idx_ >= need_) { state_ = 4; }
        break;
      case 4:                       // SUM
        state_ = 0;
        if (b == static_cast<uint8_t>(sum_)) { onFrame(need_); }
        else { ++bad_sum_; }
        break;
      default: state_ = 0; break;
    }
  }

  void onFrame(uint8_t len)
  {
    ++rx_frames_;

    std_msgs::msg::UInt8MultiArray msg;
    msg.data.assign(payload_, payload_ + len);
    pub_frame_->publish(msg);

    if (publish_counter_ && len >= 2) {
      std_msgs::msg::UInt16 c;
      c.data = static_cast<uint16_t>((payload_[0] << 8) | payload_[1]);   // 大端
      pub_counter_->publish(c);
    }

    // 不要每帧都打印(100ms 一帧会刷屏), 每 100 帧报一次
    if (rx_frames_ % 100 == 0) {
      RCLCPP_INFO(this->get_logger(), "已收 %lu 帧, 校验错 %lu",
        static_cast<unsigned long>(rx_frames_), static_cast<unsigned long>(bad_sum_));
    }
  }

  // ======================= 写: 订阅 /mcu/tx -> 打包成帧发给 STM32 =======================
  void onTx(const std_msgs::msg::UInt8MultiArray::SharedPtr msg)
  {
    if (fd_ < 0) {
      RCLCPP_WARN(this->get_logger(), "串口未打开, 丢弃发送请求");
      return;
    }
    if (msg->data.empty() || msg->data.size() > kMaxPayload) {
      RCLCPP_WARN(this->get_logger(), "payload 长度非法(1~32)");
      return;
    }

    std::vector<uint8_t> frame;
    frame.reserve(msg->data.size() + 4);
    uint8_t sum = static_cast<uint8_t>(msg->data.size());   // 校验和从 LEN 开始

    frame.push_back(kHead0);
    frame.push_back(kHead1);
    frame.push_back(static_cast<uint8_t>(msg->data.size()));
    for (const auto b : msg->data) {
      frame.push_back(b);
      sum = static_cast<uint8_t>(sum + b);
    }
    frame.push_back(sum);

    const ssize_t n = ::write(fd_, frame.data(), frame.size());
    if (n < 0) {
      RCLCPP_WARN(this->get_logger(), "写串口失败: %s", std::strerror(errno));
    } else {
      ++tx_frames_;
    }
  }

  // ======================= 成员变量 =======================
  int         fd_{-1};
  std::string port_;
  int         baud_{115200};
  bool        publish_counter_{true};

  uint8_t state_{0};                 // 解析状态机: 0..4
  uint8_t need_{0};
  uint8_t idx_{0};
  uint8_t sum_{0};
  uint8_t payload_[kMaxPayload]{};

  uint64_t rx_frames_{0};
  uint64_t tx_frames_{0};
  uint64_t bad_sum_{0};
  int      retry_{0};                // 打不开串口时的重试计数

  rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr   pub_frame_;
  rclcpp::Publisher<std_msgs::msg::UInt16>::SharedPtr            pub_counter_;
  rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr sub_tx_;
  rclcpp::TimerBase::SharedPtr                                   timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<UartBridge>());
  rclcpp::shutdown();
  return 0;
}
