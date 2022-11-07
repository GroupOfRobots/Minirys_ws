#include "minirys_ros2/nodes/IMUNode.hpp"

#include <chrono>
#include <cmath>
#include <functional>

using namespace std::chrono_literals;

IMUNode::IMUNode(I2CBus::SharedPtr i2cBus, rclcpp::NodeOptions options):
	Node("imu_vr", options),
	imu(i2cBus),
	angleFilteredPrev(0.0),
	updatePeriod(0.0) {
	RCLCPP_INFO_STREAM(this->get_logger(), "LSM6DS3: initializing");
	this->imu.initialize();
	RCLCPP_INFO_STREAM(this->get_logger(), "LSM6DS3: initialized");

	this->declare_parameter("updateFrequency", rclcpp::ParameterValue(100.0));
	this->declare_parameter("filterFactor", rclcpp::ParameterValue(0.05));
	this->declare_parameter("angleCorrection", rclcpp::ParameterValue(0.089545));
	this->declare_parameter("angleHistorySize", rclcpp::ParameterValue(4));

	this->updatePeriod = std::chrono::duration<double>(1.0 / this->get_parameter("updateFrequency").as_double());
	RCLCPP_INFO_STREAM(this->get_logger(), "Got param: update period (s) " << this->updatePeriod.count());
	this->filterFactor = this->get_parameter("filterFactor").as_double();
	RCLCPP_INFO_STREAM(this->get_logger(), "Got param: filterFactor " << this->filterFactor);
	this->angleCorrection = this->get_parameter("angleCorrection").as_double();
	RCLCPP_INFO_STREAM(this->get_logger(), "Got param: angleCorrection " << this->angleCorrection);
	this->angleHistorySize = this->get_parameter("angleHistorySize").as_int();
	RCLCPP_INFO_STREAM(this->get_logger(), "Got param: angleHistorySize " << this->angleHistorySize);

	this->imuPublisher = this->create_publisher<sensor_msgs::msg::Imu>("internal/imu", 10);
	this->angularPosePublisher = this->create_publisher<minirys_msgs::msg::AngularPose>("internal/angular_pose", 10);

	this->updateTimer = this->create_wall_timer(updatePeriod, std::bind(&IMUNode::update, this));
}

void IMUNode::update() {
	// Read the clock and the raw IMU data
	auto now = this->get_clock()->now();
	double gyroX = this->imu.readFloatGyroX();
	double gyroY = this->imu.readFloatGyroY();
	double gyroZ = this->imu.readFloatGyroZ();
	double accelX = this->imu.readFloatAccelX();
	double accelY = this->imu.readFloatAccelY();
	double accelZ = this->imu.readFloatAccelZ();

	// Calculate the raw roll angle, push it into the history
	double angleRaw = std::atan2(accelX, std::sqrt(accelY * accelY + accelZ * accelZ)) - this->angleCorrection;
	this->angleRawHistory.push_front(angleRaw);
	while (this->angleRawHistory.size() > this->angleHistorySize) {
		this->angleRawHistory.pop_back();
	}

	// Calculate the average of last N angles
	double angleSum = std::accumulate(
		this->angleRawHistory.begin(),
		this->angleRawHistory.end(),
		0.0
	);
	double angleAvg = angleSum / this->angleRawHistory.size();

	// Calculate the filtered angle and save it
	double periodInS = this->updatePeriod.count() / 1000.0;
	double periodInHz = 1000.0 / this->updatePeriod.count();
	double angleUpdated = this->angleFilteredPrev + gyroY * periodInS;
	double angleFiltered = this->filterFactor * angleAvg + angleUpdated * (1.0 - this->filterFactor);
	double angularVelocity = (angleFiltered - this->angleFilteredPrev) * periodInHz;
	this->angleFilteredPrev = angleFiltered;

	// Prepare the messages
	auto imuMessage = sensor_msgs::msg::Imu();
	imuMessage.header.frame_id = "imu";
	imuMessage.header.stamp = now;
	imuMessage.angular_velocity.x = gyroX;
	imuMessage.angular_velocity.y = gyroY;
	imuMessage.angular_velocity.z = gyroZ;
	imuMessage.linear_acceleration.x = accelX;
	imuMessage.linear_acceleration.y = accelY;
	imuMessage.linear_acceleration.z = accelZ;

	auto poseMessage = minirys_msgs::msg::AngularPose();
	poseMessage.header.frame_id = "imu";
	poseMessage.header.stamp = now;
	poseMessage.angular_position = angleFiltered;
	poseMessage.angular_velocity = angularVelocity;

	// Send the messages
	this->imuPublisher->publish(imuMessage);
	this->angularPosePublisher->publish(poseMessage);
}
