cmake_minimum_required(VERSION 2.8.3)
project(mqtt_demo)
add_compile_options(-std=c++11)

include_directories(
  include
)

add_executable(mqtt_demo src/main.cpp src/mqtt_handler.cpp)

target_link_libraries(mqtt_demo
  jsoncpp
  paho-mqtt3a
  pthread
)
