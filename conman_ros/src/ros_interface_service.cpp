/** Copyright (c) 2013, Jonathan Bohren, all rights reserved. 
 * This software is released under the BSD 3-clause license, for the details of
 * this license, please see LICENSE.txt at the root of this repository. 
 */

#include <rtt/plugin/ServicePlugin.hpp>

#include <rtt/deployment/ComponentLoader.hpp>

#include "ros_interface_service.h"

using namespace conman_ros;

ROSInterfaceService::ROSInterfaceService(RTT::TaskContext* owner) :
  RTT::Service("conman_ros",owner),
  scheme(dynamic_cast<conman::Scheme*>(owner))
{ 
  // Make sure we're attached to a scheme
  if(!scheme) { 
    std::string err_text = "Attmpted to load the Conman ROS interface on a component which isn't a scheme!";
    RTT::log(RTT::Error) << err_text << RTT::endlog();
    throw std::runtime_error(err_text);
  }

  // Connect operation callers
  RTT::log(RTT::Debug) << "Connecting conamn_ros operation callers..." << RTT::endlog();
  getBlocks = scheme->getOperation("getBlocks");
  getGroups = scheme->getOperation("getGroups");
  switchBlocks = scheme->getOperation("switchBlocks");

  // Create ros-control operation bindings
  RTT::log(RTT::Debug) << "Creating ros_control service servers..." << RTT::endlog();
  roscontrol = owner->provides("roscontrol");
  roscontrol->addOperation("listControllerTypes", &ROSInterfaceService::listControllerTypesCB, this);
  roscontrol->addOperation("listControllers", &ROSInterfaceService::listControllersCB, this);
  roscontrol->addOperation("loadController", &ROSInterfaceService::loadControllerCB, this);
  roscontrol->addOperation("reloadControllerLibraries", &ROSInterfaceService::reloadControllerLibrariesCB, this);
  roscontrol->addOperation("switchController", &ROSInterfaceService::switchControllerCB, this);
  roscontrol->addOperation("unloadController", &ROSInterfaceService::unloadControllerCB, this);

  // Load the rosservice service
  RTT::log(RTT::Debug) << "Getting rtt_roscomm service service..." << RTT::endlog();
  rosservice = owner->getProvider<rtt_rosservice::ROSService>("rosservice");

  RTT::log(RTT::Debug) << "Connecting ros_control service servers..." << RTT::endlog();
  rosservice->connect("roscontrol.listControllerTypes",
                     "controller_manager/list_controller_types",
                     "controller_manager_msgs/ListControllerTypes");

  rosservice->connect("roscontrol.listControllers",
                     "controller_manager/list_controllers",
                     "controller_manager_msgs/ListControllers");
  
  rosservice->connect("roscontrol.loadController",
                     "controller_manager/load_controller",
                     "controller_manager_msgs/LoadController");

  rosservice->connect("roscontrol.reloadControllerLibraries",
                     "controller_manager/reload_controller_libraries",
                     "controller_manager_msgs/ReloadControllerLibraries");

  rosservice->connect("roscontrol.switchController",
                     "controller_manager/switch_controller",
                     "controller_manager_msgs/SwitchController");

  rosservice->connect("roscontrol.unloadController",
                     "controller_manager/unload_controller",
                     "controller_manager_msgs/UnloadController"); 

}

bool ROSInterfaceService::listControllerTypesCB(
    controller_manager_msgs::ListControllerTypes::Request &req,
    controller_manager_msgs::ListControllerTypes::Response& resp)
{

  return false;
}
bool ROSInterfaceService::listControllersCB(
    controller_manager_msgs::ListControllers::Request &req,
    controller_manager_msgs::ListControllers::Response& resp)
{
  const std::vector<std::string> block_names = getBlocks();
  const std::vector<std::string> group_names = getGroups();

  resp.controller.reserve(block_names.size() + group_names.size());

  for(std::vector<std::string>::const_iterator it = block_names.begin();
      it != block_names.end();
      ++it)
  {
    RTT::TaskContext *block_task = scheme->getPeer(*it);
    controller_manager_msgs::ControllerState cs;
    cs.name = *it;
    cs.type = "OROCOS COMPONENT";
    cs.state = (block_task->getTaskState() == RTT::TaskContext::Running) ? "running" : "stopped";
    resp.controller.push_back(cs);
  }

  for(std::vector<std::string>::const_iterator it = group_names.begin();
      it != group_names.end();
      ++it)
  {
    controller_manager_msgs::ControllerState cs;
    cs.name = *it;
    cs.type = "CONMAN GROUP";
    resp.controller.push_back(cs);
  }

  return true;
}
bool ROSInterfaceService::loadControllerCB(
    controller_manager_msgs::LoadController::Request &req,
    controller_manager_msgs::LoadController::Response& resp)
{
  return false;
}
bool ROSInterfaceService::reloadControllerLibrariesCB(
    controller_manager_msgs::ReloadControllerLibraries::Request &req,
    controller_manager_msgs::ReloadControllerLibraries::Response& resp)
{
  return false;
}
bool ROSInterfaceService::switchControllerCB(
    controller_manager_msgs::SwitchController::Request &req,
    controller_manager_msgs::SwitchController::Response& resp)
{
  RTT::log(RTT::Debug) << "Handling ros_control switch controllers request..." << RTT::endlog();
  resp.ok = switchBlocks(
      req.stop_controllers,
      req.start_controllers,
      req.strictness == controller_manager_msgs::SwitchController::Request::STRICT,
      false);

  return true;
}
bool ROSInterfaceService::unloadControllerCB(
    controller_manager_msgs::UnloadController::Request &req,
    controller_manager_msgs::UnloadController::Response& resp)
{
  return false;
}

ORO_SERVICE_NAMED_PLUGIN(conman_ros::ROSInterfaceService, "conman_ros");


