"""uart_bridge launch 文件（只是参数配置，不是业务代码）

用法：
    ros2 launch uart_bridge uart_bridge.launch.py port:=/dev/ttyUSB0 baud:=115200
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    port = LaunchConfiguration('port')
    baud = LaunchConfiguration('baud')

    return LaunchDescription([
        DeclareLaunchArgument('port', default_value='/dev/ttyUSB0',
                              description='STM32 所在的串口设备'),
        DeclareLaunchArgument('baud', default_value='115200',
                              description='波特率(与 MCU 一致)'),

        Node(
            package='uart_bridge',
            executable='uart_bridge_node',
            name='uart_bridge',
            output='screen',
            parameters=[{
                'port': ParameterValue(port, value_type=str),
                'baud': ParameterValue(baud, value_type=int),
                'publish_counter': True,
            }],
        ),
    ])
