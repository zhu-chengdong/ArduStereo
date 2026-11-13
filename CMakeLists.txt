cmake_minimum_required(VERSION 3.16)
project(ardustereo VERSION 0.4.0 LANGUAGES CXX)

option(ARDUSTEREO_BUILD_ROS "Build the ROS 1 node (requires catkin)" ON)
option(ARDUSTEREO_BUILD_OPENCV "Build the OpenCV V4L2 source" ${ARDUSTEREO_BUILD_ROS})
option(ARDUSTEREO_SANITIZERS "Enable address/undefined sanitizers for local tests" OFF)
include(CTest)
include(GNUInstallDirs)

find_package(Threads REQUIRED)
add_library(ardustereo_core src/frame_ops.cpp src/capture.cpp src/config.cpp src/diagnostics.cpp src/controls.cpp)
target_link_libraries(ardustereo_core PUBLIC Threads::Threads)
target_compile_features(ardustereo_core PUBLIC cxx_std_14)
target_include_directories(ardustereo_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ardustereo_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(ARDUSTEREO_SANITIZERS)
    target_compile_options(ardustereo_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ardustereo_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

if(ARDUSTEREO_BUILD_OPENCV)
  find_package(OpenCV 4 REQUIRED COMPONENTS core videoio)
  add_library(ardustereo_opencv src/opencv_capture.cpp src/v4l2_controls.cpp src/runtime_controls.cpp)
  target_compile_features(ardustereo_opencv PUBLIC cxx_std_14)
  target_include_directories(ardustereo_opencv PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(ardustereo_opencv PUBLIC ardustereo_core ${OpenCV_LIBRARIES})
endif()

if(ARDUSTEREO_BUILD_ROS)
  if(NOT ARDUSTEREO_BUILD_OPENCV)
    message(FATAL_ERROR "The ROS node requires ARDUSTEREO_BUILD_OPENCV=ON")
  endif()
  find_package(catkin REQUIRED COMPONENTS roscpp sensor_msgs camera_info_manager diagnostic_msgs message_generation)
  add_service_files(FILES GetControls.srv SetControls.srv)
  generate_messages()
  catkin_package(INCLUDE_DIRS include LIBRARIES ardustereo_core ardustereo_opencv
    CATKIN_DEPENDS roscpp sensor_msgs camera_info_manager diagnostic_msgs message_runtime DEPENDS OpenCV)
  add_library(ardustereo_ros STATIC src/ros_node.cpp)
  add_dependencies(ardustereo_ros ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
  target_include_directories(ardustereo_ros PRIVATE ${catkin_INCLUDE_DIRS})
  target_link_libraries(ardustereo_ros PUBLIC ardustereo_opencv ${catkin_LIBRARIES})
  add_executable(ardustereo_node src/ardustereo_node.cpp)
  target_include_directories(ardustereo_node PRIVATE ${catkin_INCLUDE_DIRS})
  target_link_libraries(ardustereo_node PRIVATE ardustereo_ros)
  catkin_install_python(PROGRAMS scripts/experiment_tools.py scripts/viewer.py
    DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
  install(FILES scripts/session_store.py scripts/viewer_core.py scripts/viewer_ros.py scripts/viewer_recording.py DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
  install(TARGETS ardustereo_core ardustereo_opencv ardustereo_node
    ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
    LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
    RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
  install(DIRECTORY include/ardustereo/ DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION})
  install(DIRECTORY launch config docs web DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
  install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md
    DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION})
  if(CATKIN_ENABLE_TESTING)
    find_package(rostest REQUIRED)
    catkin_install_python(PROGRAMS test/test_ros_interface.py
      test/test_ros_diagnostics.py test/test_experiment_tools.py
      DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
    add_executable(ardustereo_fault_test_node test/test_fault_node.cpp)
    target_include_directories(ardustereo_fault_test_node PRIVATE src ${catkin_INCLUDE_DIRS})
    target_link_libraries(ardustereo_fault_test_node PRIVATE ardustereo_ros)
    add_rostest(test/stereo_interface.test)
    add_rostest(test/diagnostics.test)
    add_rostest(test/experiment_tools.test)
    catkin_install_python(PROGRAMS test/test_runtime_controls.py DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
    add_rostest(test/runtime_controls.test)
    add_test(NAME session_store_contracts COMMAND ${PYTHON_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/test/test_session_store.py)
  endif()
else()
  install(TARGETS ardustereo_core EXPORT ArduStereoTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY include/ardustereo/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ardustereo)
  install(EXPORT ArduStereoTargets NAMESPACE ArduStereo:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ArduStereo)
  if(ARDUSTEREO_BUILD_OPENCV)
    install(TARGETS ardustereo_opencv EXPORT ArduStereoTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()
  include(CMakePackageConfigHelpers)
  configure_package_config_file(cmake/ArduStereoConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ArduStereoConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ArduStereo)
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ArduStereoConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ArduStereoConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ArduStereoConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ArduStereo)
endif()

if(BUILD_TESTING)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(ardustereo_startup_controls_test test/test_startup_controls.cpp
      src/v4l2_controls.cpp src/runtime_controls.cpp)
    target_compile_features(ardustereo_startup_controls_test PRIVATE cxx_std_14)
    target_include_directories(ardustereo_startup_controls_test PRIVATE include src)
    target_compile_options(ardustereo_startup_controls_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(ardustereo_startup_controls_test PRIVATE
      -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl)
    add_test(NAME startup_controls_contracts COMMAND ardustereo_startup_controls_test)
  endif()
  add_executable(ardustereo_controls_test test/test_controls.cpp)
  target_link_libraries(ardustereo_controls_test PRIVATE ardustereo_core)
  add_test(NAME controls_contracts COMMAND ardustereo_controls_test)
  add_executable(ardustereo_core_test test/test_core.cpp)
  target_link_libraries(ardustereo_core_test PRIVATE ardustereo_core)
  add_test(NAME core_contracts COMMAND ardustereo_core_test)
  add_executable(ardustereo_diagnostics_test test/test_diagnostics.cpp)
  target_link_libraries(ardustereo_diagnostics_test PRIVATE ardustereo_core)
  add_test(NAME diagnostics_contracts COMMAND ardustereo_diagnostics_test)
endif()
