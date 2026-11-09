// Test-only executable. Never installed; production node has no fault parameters.
#include "ros_node.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
class FaultSource : public ardustereo::CaptureSource {
 public:
  explicit FaultSource(std::string scenario) : scenario_(std::move(scenario)) {}
  void open(const ardustereo::CaptureConfig& c) override {
    if(scenario_=="open_exception") throw std::runtime_error("injected open exception");
    c_=c;
  }
  bool read(ardustereo::CapturedFrame& frame) override {
    ++reads_;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if(scenario_=="empty_limit" && reads_>10) return false;
    if(scenario_=="recover" && reads_>=11 && reads_<=15) return false;
    if(scenario_=="read_exception" && reads_>10) throw std::runtime_error("injected read exception");
    if(scenario_=="finite_stall" && reads_==11)
      std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    frame.image.width=c_.width;frame.image.height=c_.height;
    frame.image.data.assign(static_cast<std::size_t>(c_.width)*c_.height,
                            static_cast<std::uint8_t>(reads_));
    frame.received_system_ns=ardustereo::system_time_ns();
    if(scenario_=="bad_frame" && reads_>10) frame.image.data.pop_back();
    return true;
  }
  void close() noexcept override { ROS_INFO("FAULT_TEST_SOURCE_CLOSED"); }
  std::string description() const override { return "INJECTED TEST SOURCE: "+scenario_; }
 private:
  std::string scenario_; ardustereo::CaptureConfig c_;unsigned reads_=0;
};
}
int main(int argc,char** argv) {
  ros::init(argc,argv,"ardustereo_fault_test");
  std::vector<std::string> args;ros::removeROSArgs(argc,argv,args);
  if(args.size()!=2) return 2;
  ros::NodeHandle nh,private_nh("~");
  return ardustereo::run_ros_node(nh,private_nh,
      std::unique_ptr<ardustereo::CaptureSource>(new FaultSource(args[1])));
}
