#include "ros_node.hpp"

int main(int argc, char** argv) {
  ros::init(argc, argv, "ardustereo");
  ros::NodeHandle nh;
  ros::NodeHandle private_nh("~");
  return ardustereo::run_ros_node(nh, private_nh);
}
