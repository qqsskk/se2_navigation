/*
 * PriusControllerRos.cpp
 *
 *  Created on: Apr 6, 2020
 *      Author: jelavice
 */

#include "car_demo/PriusControllerRos.hpp"

#include "prius_msgs/Control.h"
#include "pure_pursuit_ros/AckermannSteeringControllerRos.hpp"
#include "pure_pursuit_ros/loaders.hpp"
#include "pure_pursuit_ros/SimplePathTrackerRos.hpp"
#include "se2_navigation_msgs/ControllerCommand.hpp"

namespace car_demo {

PriusControllerRos::PriusControllerRos(ros::NodeHandlePtr nh)
    : nh_(nh)
{
  initRos();
}

PriusControllerRos::~PriusControllerRos() = default;

void PriusControllerRos::initialize(double dt)
{
  dt_ = dt;
  createControllerAndLoadParameters();
  ROS_INFO_STREAM("PriusControllerRos: Initialization done");
}

void PriusControllerRos::createControllerAndLoadParameters()
{
  const std::string controllerParametersFilename = nh_->param<std::string>(
      "/prius_controller_ros_parameters_filename", "");

  namespace pp = pure_pursuit;
  auto velocityParams = pp::loadConstantVelocityControllerParameters(controllerParametersFilename);
  velocityParams.timestep_ = dt_;
  std::shared_ptr<pp::LongitudinalVelocityController> velocityController =
      pp::createConstantVelocityController(velocityParams);

  auto ackermannParams = pp::loadAckermannSteeringControllerParameters(
      controllerParametersFilename);
  ackermannParams.dt_ = dt_;
  std::shared_ptr<pp::HeadingController> headingController =
      pp::createAckermannSteeringControllerRos(ackermannParams, nh_.get());

  std::shared_ptr<pp::ProgressValidator> progressValidator = pp::createProgressValidator(
      pp::loadProgressValidatorParameters(controllerParametersFilename));

  std::shared_ptr<pp::PathPreprocessor> pathPreprocessor = pp::createPathPreprocessor(
      pp::loadPathPreprocessorParameters(controllerParametersFilename));

  auto pathTrackerParameters = pp::loadSimplePathTrackerParameters(controllerParametersFilename);
  pathTracker_ = pp::createSimplePathTrackerRos(pathTrackerParameters, velocityController,
                                                headingController, progressValidator,
                                                pathPreprocessor, nh_.get());
  if (pathTracker_ == nullptr) {
    throw std::runtime_error("PriusControllerRos:: pathTracker_ is nullptr");
  }
}
void PriusControllerRos::advance()
{
  prius_msgs::PriusControl control;

  const bool readyToTrack = planReceived_ && receivedStartTrackingCommand_;

  if (!readyToTrack) {
    publishControl(prius_msgs::PriusControl::getFailProofControlCommand());
    return;
  }

  prius_msgs::PriusControl controlCommand;
  if (!pathTracker_->advance()) {
      ROS_ERROR_STREAM("Failed to advance path tracker.");
      controlCommand = prius_msgs::PriusControl::getFailProofControlCommand();
      stopTracking();
      publishTrackingStatus_ = true;
    } else {
      const double steering = pathTracker_->getSteeringAngle();
      const double velocity = pathTracker_->getLongitudinalVelocity();
      //todo convert all of this crep to PriusControl
    }

  control.gear_ = prius_msgs::PriusControl::Gear::FORWARD;
  control.steer_ = 0.8;
  control.throttle_ = 0.02;

  publishControl(control);
}

void PriusControllerRos::stopTracking(){
  ROS_INFO_STREAM("PriusControllerRos stopped tracking");
  currentlyExecutingPlan_ = false;
  receivedStartTrackingCommand_ = false;
  planReceived_ = false;
  pathTracker_->stopTracking();
}

void PriusControllerRos::publishControl(const prius_msgs::PriusControl &ctrl) const
{
  priusControlPub_.publish(prius_msgs::convert(ctrl));
}
void PriusControllerRos::initRos()
{
  //todo remove hardcoded paths
  priusControlPub_ = nh_->advertise<prius_msgs::Control>("/prius_controls", 1, false);
  priusStateSub_ = nh_->subscribe("/prius/base_pose_ground_truth", 1,
                                  &PriusControllerRos::priusStateCallback, this);
  priusCurrentStateService_ = nh_->advertiseService("/prius/get_current_state_service",
                                                    &PriusControllerRos::currentStateRequestService,
                                                    this);
  controllerCommandService_ = nh_->advertiseService("/prius/controller_command_service",
                                                    &PriusControllerRos::controllerCommandService,
                                                    this);
  pathSub_ = nh_->subscribe("/se2_planner_node/ompl_rs_planner_ros/path", 1,
                            &PriusControllerRos::pathCallback, this);
}

void PriusControllerRos::pathCallback(const se2_navigation_msgs::PathMsg &pathMsg)
{
  const se2_navigation_msgs::Path path = se2_navigation_msgs::convert(pathMsg);

  if (currentlyExecutingPlan_) {
    ROS_WARN_STREAM("PathFollowerRos:: Robot is tracking the previous plan. Rejecting this one.");
    return;
  }

  if (path.segment_.empty()) {
    ROS_WARN_STREAM("Path follower received an empty plan!");
    return;
  }

  ROS_INFO_STREAM(
      "PathFollowerRos subscriber received a plan, num segments: " << path.segment_.size());

  planReceived_ = true;
}

void PriusControllerRos::priusStateCallback(const nav_msgs::Odometry &odometry)
{
  priusState_ = odometry;
}

bool PriusControllerRos::currentStateRequestService(CurrentStateService::Request &req,
                                                    CurrentStateService::Response &res)
{
  res.pose = priusState_.pose.pose;
  res.twist = priusState_.twist.twist;

  return true;
}
bool PriusControllerRos::controllerCommandService(ControllerCommandService::Request &req,
                                                  ControllerCommandService::Response &res)
{
  const auto command = se2_navigation_msgs::convert(req.command);
  using Command = se2_navigation_msgs::ControllerCommand::Command;
  switch (command.command_) {
    case Command::StartTracking: {
      processStartTrackingCommand();
      break;
    }
    case Command::StopTracking: {
      processAbortTrackingCommand();
      break;
    }
    default: {
      ROS_WARN_STREAM("PATH FOLLOWER ROS: Unknown command");
    }
  }

  return true;
}

void PriusControllerRos::processStartTrackingCommand()
{
  if (!planReceived_) {
    ROS_WARN_STREAM(
        "PriusControllerRos:: Rejecting  the start command since the robot hasn't received a plan yet");
    return;
  }

  if (currentlyExecutingPlan_) {
    ROS_WARN_STREAM(
        "PriusControllerRos:: Rejecting  the start command since the robot is already executing another plan");;
    return;
  }

  ROS_WARN_STREAM("PriusControllerRos:: Start tracking requested");

  currentlyExecutingPlan_ = true;
  receivedStartTrackingCommand_ = true;
}
void PriusControllerRos::processAbortTrackingCommand()
{
  if (!currentlyExecutingPlan_) {
    ROS_WARN_STREAM("PriusControllerRos:: Not tracking any plans at the moment, cannot stop");
    return;
  } else {
    stopTracking();
  }
}

} /* namespace car_demo*/
