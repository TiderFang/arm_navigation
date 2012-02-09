/*********************************************************************
 * Software License Agreement (BSD License)
 * 
 *  Copyright (c) 2011, Willow Garage, Inc.
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 * 
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *
 *********************************************************************/

/* \author: Sachin Chitta*/

#include <ros/ros.h>
#include <actionlib/client/simple_action_client.h>
#include <arm_navigation_msgs/MoveArmAction.h>
#include <arm_navigation_msgs/GetMotionPlan.h>
#include <pr2_both_arms_tests/pr2_arm_navigation_utils.h>
#include <geometry_msgs/Pose.h>

#include <arm_navigation_msgs/SetPlanningSceneDiff.h>

static const std::string SET_PLANNING_SCENE_DIFF_SERVICE="/environment_server/set_planning_scene_diff";

int main(int argc, char **argv){
  ros::init (argc, argv, "move_arm_both_arms_test");
  ros::NodeHandle nh;
  actionlib::SimpleActionClient<arm_navigation_msgs::MoveArmAction> move_arm("move_arm",true);

  arm_navigation_utils::ArmNavigationUtils arm_nav_utils;
  arm_nav_utils.takeStaticMap();

  ros::service::waitForService(SET_PLANNING_SCENE_DIFF_SERVICE);
  move_arm.waitForServer();
  move_arm.waitForServer();
  ROS_INFO("Connected to server");

  ros::ServiceClient set_planning_scene_diff_client;
  set_planning_scene_diff_client = nh.serviceClient<arm_navigation_msgs::SetPlanningSceneDiff>(SET_PLANNING_SCENE_DIFF_SERVICE);
  arm_navigation_msgs::MoveArmGoal goal_right;
  goal_right.motion_plan_request.group_name = "right_arm";
  goal_right.motion_plan_request.num_planning_attempts = 1;
  goal_right.motion_plan_request.allowed_planning_time = ros::Duration(5.0);
  nh.param<std::string>("planner_id",goal_right.motion_plan_request.planner_id,std::string(""));
  nh.param<std::string>("planner_service_name",goal_right.planner_service_name,std::string("ompl_planning/plan_kinematic_path"));
  goal_right.motion_plan_request.goal_constraints.position_constraints.resize(1);
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].header.stamp = ros::Time::now();
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].header.frame_id = "torso_lift_link";
    
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].link_name = "r_wrist_roll_link";
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].position.x = 0.75;
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].position.y = -0.188;
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].position.z = 0;
    
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.type = arm_navigation_msgs::Shape::BOX;
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);

  goal_right.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_orientation.w = 1.0;
  goal_right.motion_plan_request.goal_constraints.position_constraints[0].weight = 1.0;

  goal_right.motion_plan_request.goal_constraints.orientation_constraints.resize(1);
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].header.stamp = ros::Time::now();
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].header.frame_id = "torso_lift_link";    
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].link_name = "r_wrist_roll_link";
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.x = 0.0;
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.y = 0.0;
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.z = 0.0;
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.w = 1.0;
    
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_roll_tolerance = 0.04;
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_pitch_tolerance = 0.04;
  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_yaw_tolerance = 0.04;

  goal_right.motion_plan_request.goal_constraints.orientation_constraints[0].weight = 1.0;

  if (nh.ok())
  {
    bool finished_within_time = false;
    move_arm.sendGoal(goal_right);
    finished_within_time = move_arm.waitForResult(ros::Duration(200.0));
    if (!finished_within_time)
    {
      move_arm.cancelGoal();
      ROS_INFO("Timed out achieving goal A");
    }
    else
    {
      actionlib::SimpleClientGoalState state = move_arm.getState();
      bool success = (state == actionlib::SimpleClientGoalState::SUCCEEDED);
      if(success)
        ROS_INFO("Action finished: %s",state.toString().c_str());
      else
        ROS_INFO("Action failed: %s",state.toString().c_str());
    }
  }


  arm_navigation_msgs::MoveArmGoal goal_left;
  goal_left.motion_plan_request.group_name = "left_arm";
  goal_left.motion_plan_request.num_planning_attempts = 1;
  goal_left.motion_plan_request.allowed_planning_time = ros::Duration(5.0);
  nh.param<std::string>("planner_id",goal_left.motion_plan_request.planner_id,std::string(""));
  nh.param<std::string>("planner_service_name",goal_left.planner_service_name,std::string("ompl_planning/plan_kinematic_path"));
  goal_left.motion_plan_request.goal_constraints.position_constraints.resize(1);
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].header.stamp = ros::Time::now();
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].header.frame_id = "torso_lift_link";
    
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].link_name = "l_wrist_roll_link";
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].position.x = 0.75;
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].position.y = 0.188;
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].position.z = 0;
    
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.type = arm_navigation_msgs::Shape::BOX;
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);

  goal_left.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_orientation.w = 1.0;
  goal_left.motion_plan_request.goal_constraints.position_constraints[0].weight = 1.0;

  goal_left.motion_plan_request.goal_constraints.orientation_constraints.resize(1);
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].header.stamp = ros::Time::now();
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].header.frame_id = "torso_lift_link";    
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].link_name = "l_wrist_roll_link";
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.x = 0.0;
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.y = 0.0;
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.z = 0.0;
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.w = 1.0;
    
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_roll_tolerance = 0.04;
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_pitch_tolerance = 0.04;
  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_yaw_tolerance = 0.04;

  goal_left.motion_plan_request.goal_constraints.orientation_constraints[0].weight = 1.0;

  if (nh.ok())
  {
    bool finished_within_time = false;
    move_arm.sendGoal(goal_left);
    finished_within_time = move_arm.waitForResult(ros::Duration(200.0));
    if (!finished_within_time)
    {
      move_arm.cancelGoal();
      ROS_INFO("Timed out achieving goal A");
    }
    else
    {
      actionlib::SimpleClientGoalState state = move_arm.getState();
      bool success = (state == actionlib::SimpleClientGoalState::SUCCEEDED);
      if(success)
        ROS_INFO("Action finished: %s",state.toString().c_str());
      else
        ROS_INFO("Action failed: %s",state.toString().c_str());
    }
  }

  arm_navigation_msgs::SetPlanningSceneDiff::Request get_req;
  arm_navigation_msgs::SetPlanningSceneDiff::Response get_res;
  arm_navigation_msgs::CollisionObject pole;  
  pole.header.stamp = ros::Time::now();
  pole.header.frame_id = "torso_lift_link";
  pole.id = "pole";
  pole.operation.operation = arm_navigation_msgs::CollisionObjectOperation::ADD;
  pole.shapes.resize(1);
  pole.shapes[0].type = arm_navigation_msgs::Shape::CYLINDER;
  pole.shapes[0].dimensions.resize(2);
  pole.shapes[0].dimensions[0] = 0.01;
  pole.shapes[0].dimensions[1] = 1.5;
  pole.poses.resize(1);
  pole.poses[0].position.x = .75;
  pole.poses[0].position.y = 0.0;
  pole.poses[0].position.z = 0.0;
  pole.poses[0].orientation.w = 1.0;
  get_req.planning_scene_diff.collision_objects.push_back(pole);

  set_planning_scene_diff_client.call(get_req, get_res);

  //  arm_navigation_msgs::GetMotionPlan::Request request;
  //  arm_navigation_msgs::GetMotionPlan::Response response;
  arm_navigation_msgs::MoveArmGoal request;
  request.planner_service_name = "ompl_planning/plan_kinematic_path";

  request.motion_plan_request.group_name = "both_arms_cartesian";
  request.motion_plan_request.num_planning_attempts = 1;
  request.motion_plan_request.allowed_planning_time = ros::Duration(5.0);
  request.motion_plan_request.planner_id= std::string("");

  request.motion_plan_request.goal_constraints.position_constraints.resize(1);
  request.motion_plan_request.goal_constraints.position_constraints[0].header.stamp = ros::Time::now();
  request.motion_plan_request.goal_constraints.position_constraints[0].header.frame_id = "torso_lift_link";
    
  request.motion_plan_request.goal_constraints.position_constraints[0].link_name = "two_arms_object";
  request.motion_plan_request.goal_constraints.position_constraints[0].position.x = 0.75;
  request.motion_plan_request.goal_constraints.position_constraints[0].position.y = 0.0;
  request.motion_plan_request.goal_constraints.position_constraints[0].position.z = 0.1;
    
  request.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.type = arm_navigation_msgs::Shape::BOX;
  request.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);
  request.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);
  request.motion_plan_request.goal_constraints.position_constraints[0].constraint_region_shape.dimensions.push_back(0.02);

  request.motion_plan_request.goal_constraints.orientation_constraints.resize(1);
  request.motion_plan_request.goal_constraints.orientation_constraints[0].header.stamp = ros::Time::now();
  request.motion_plan_request.goal_constraints.orientation_constraints[0].header.frame_id = "torso_lift_link";    
  request.motion_plan_request.goal_constraints.orientation_constraints[0].link_name = "two_arms_object";
  request.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.x = 0.0;
  request.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.y = 0.0;
  request.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.z = 0.0;
  request.motion_plan_request.goal_constraints.orientation_constraints[0].orientation.w = 1.0;
    
  request.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_roll_tolerance = 0.04;
  request.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_pitch_tolerance = 0.04;
  request.motion_plan_request.goal_constraints.orientation_constraints[0].absolute_yaw_tolerance = 0.04;

  request.motion_plan_request.start_state.multi_dof_joint_state.poses.resize(2);
  request.motion_plan_request.start_state.multi_dof_joint_state.frame_ids.resize(2);
  request.motion_plan_request.start_state.multi_dof_joint_state.child_frame_ids.resize(2);

  request.motion_plan_request.start_state.multi_dof_joint_state.frame_ids[0] = "two_arms_object";
  request.motion_plan_request.start_state.multi_dof_joint_state.frame_ids[1] = "two_arms_object";

  request.motion_plan_request.start_state.multi_dof_joint_state.child_frame_ids[0] = "r_wrist_roll_link";
  request.motion_plan_request.start_state.multi_dof_joint_state.child_frame_ids[1] = "l_wrist_roll_link";

  geometry_msgs::Pose pose_offset;
  pose_offset.position.x = 0.0;
  pose_offset.position.y = -0.188;
  pose_offset.position.z = 0.0;

  pose_offset.orientation.x = 0.0;
  pose_offset.orientation.y = 0.0;
  pose_offset.orientation.z = 0.0;
  pose_offset.orientation.w = 1.0;

  request.motion_plan_request.start_state.multi_dof_joint_state.poses[0] = pose_offset;
  pose_offset.position.y = 0.188;
  request.motion_plan_request.start_state.multi_dof_joint_state.poses[1] = pose_offset;

  request.motion_plan_request.goal_constraints.joint_constraints.resize(2);

  request.motion_plan_request.goal_constraints.joint_constraints[0].joint_name="l_upper_arm_roll_joint";
  request.motion_plan_request.goal_constraints.joint_constraints[0].position = 0.0;
  request.motion_plan_request.goal_constraints.joint_constraints[0].tolerance_below = 0.05;
  request.motion_plan_request.goal_constraints.joint_constraints[0].tolerance_above = 0.05;

  request.motion_plan_request.goal_constraints.joint_constraints[1].joint_name="r_upper_arm_roll_joint";
  request.motion_plan_request.goal_constraints.joint_constraints[1].position = 0.0;
  request.motion_plan_request.goal_constraints.joint_constraints[1].tolerance_below = 0.05;
  request.motion_plan_request.goal_constraints.joint_constraints[1].tolerance_above = 0.05;

  /*
  ros::ServiceClient service_client = nh.serviceClient<arm_navigation_msgs::GetMotionPlan>("ompl_planning/plan_kinematic_path");
  service_client.call(request,response);
  if(response.error_code.val != response.error_code.SUCCESS)
  {
    ROS_ERROR("Motion planning failed");
  }
  else
  {
    ROS_INFO("Motion planning succeeded");
    }*/

  sleep(5.0);
  if (nh.ok())
  {
    bool finished_within_time = false;
    move_arm.sendGoal(request);
    finished_within_time = move_arm.waitForResult(ros::Duration(200.0));
    if (!finished_within_time)
    {
      move_arm.cancelGoal();
      ROS_INFO("Timed out achieving goal A");
    }
    else
    {
      actionlib::SimpleClientGoalState state = move_arm.getState();
      bool success = (state == actionlib::SimpleClientGoalState::SUCCEEDED);
      if(success)
        ROS_INFO("Action finished: %s",state.toString().c_str());
      else
        ROS_INFO("Action failed: %s",state.toString().c_str());
    }
  }



  ros::shutdown();
}
