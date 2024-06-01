/****************************************************************************
 *
 *   Copyright (c) 2023-2024 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include "SeseOmni.hpp"
# define M_PI 3.14159265358979323846

SeseOmni::SeseOmni() : ModuleParams(nullptr),
					   ScheduledWorkItem(MODULE_NAME, px4::wq_configurations::rate_ctrl)
{
	updateParams();
}

bool SeseOmni::init()
{
	ScheduleOnInterval(10_ms); // 100 Hz
	return true;
}

void SeseOmni::updateParams()
{
	ModuleParams::updateParams();
	pid_init(&_att_pid, PID_MODE_DERIVATIV_CALC, 0.01f);
	pid_set_parameters(&_att_pid,
					   att_p_gain.get(), // P
					   att_i_gain.get(), // I
					   att_d_gain.get(), // D
					   10.0f, // Integral limit
					   1.0f); // Output limit
}

void SeseOmni::Run()
{
	if (should_exit())
	{
		ScheduleClear();
		exit_and_cleanup();
	}

	hrt_abstime now = hrt_absolute_time();


	if (_parameter_update_sub.updated())
	{
		parameter_update_s parameter_update;
		_parameter_update_sub.copy(&parameter_update);
		updateParams();
	}

	if (_vehicle_control_mode_sub.updated())
	{
		vehicle_control_mode_s vehicle_control_mode{};

		if (_vehicle_control_mode_sub.copy(&vehicle_control_mode))
		{
			_mission_driving = vehicle_control_mode.flag_control_auto_enabled;
		}
	}

	if (_vehicle_status_sub.updated())
	{
		vehicle_status_s vehicle_status{};

		if (_vehicle_status_sub.copy(&vehicle_status))
		{
			_manual_driving = (vehicle_status.nav_state == vehicle_status_s::NAVIGATION_STATE_MANUAL);
			_acro_driving = (vehicle_status.nav_state == vehicle_status_s::NAVIGATION_STATE_ACRO);
		}
	}

	if (_manual_driving)
	{
		// Manual mode
		// directly produce setpoints from the manual control setpoint (joystick)
		if (_manual_control_setpoint_sub.updated())
		{
			manual_control_setpoint_s manual_control_setpoint{};

			if (_manual_control_setpoint_sub.copy(&manual_control_setpoint))
			{
				// Create msgs
				vehicle_torque_setpoint_s torque_setpoint{};
				vehicle_thrust_setpoint_s thrust_setpoint{};
				actuator_controls_status_s status;

				thrust_setpoint.timestamp = now;
				thrust_setpoint.xyz[0] = -manual_control_setpoint.throttle * _max_speed;
				thrust_setpoint.xyz[1] = manual_control_setpoint.yaw * _max_speed;
				thrust_setpoint.xyz[2] = 0.0f;

				torque_setpoint.timestamp = now;
				torque_setpoint.xyz[0] = 0.0f;
				torque_setpoint.xyz[1] = 0.0f;
				torque_setpoint.xyz[2] = manual_control_setpoint.roll * _max_speed;

				status.timestamp = torque_setpoint.timestamp;

				for (int i = 0; i < 3; i++)
				{
					status.control_power[i] = 100.0f;
				}

				_vehicle_torque_setpoint_pub.publish(torque_setpoint);
				_vehicle_thrust_setpoint_pub.publish(thrust_setpoint);
				_actuator_controls_status_pub.publish(status);
			}
		}
	}

	else if (_acro_driving)
	{
		PX4_INFO_RAW("ACRO RUNNING");
		if (_manual_control_setpoint_sub.updated())
		{
			manual_control_setpoint_s manual_control_setpoint{};

			if (_manual_control_setpoint_sub.copy(&manual_control_setpoint))
			{
				vehicle_thrust_setpoint_s thrust_setpoint{};

				thrust_setpoint.timestamp = now;
				thrust_setpoint.xyz[0] = -manual_control_setpoint.throttle * _max_speed;
				thrust_setpoint.xyz[1] = manual_control_setpoint.yaw * _max_speed;
				thrust_setpoint.xyz[2] = 0.0f;

				_vehicle_thrust_setpoint_pub.publish(thrust_setpoint);
			}
		}
		if (_local_pos_sub.update(&_local_pos)) {
			const float dt = math::min((now - _time_stamp_last), 5000_ms) / 1e3f;
			_time_stamp_last = now;

			float desired_heading = 1.0f;

			float current_heading = _local_pos.heading;

			vehicle_torque_setpoint_s torque_setpoint{};
			actuator_controls_status_s status;

			torque_setpoint.timestamp = now;
			torque_setpoint.xyz[0] = 0.0f;
			torque_setpoint.xyz[1] = 0.0f;
			torque_setpoint.xyz[2] = pid_calculate(&_att_pid, desired_heading, current_heading, 0.0f, dt);

			status.timestamp = torque_setpoint.timestamp;

			for (int i = 0; i < 3; i++)
			{
				status.control_power[i] = 100.0f;
			}

			// Tutaj bedzie jakas magia
			vehicle_thrust_setpoint_s thrust_setpoint{};
			thrust_setpoint.timestamp = _time_stamp_last;

			float position_north = _local_pos.x;
			float position_east = _local_pos.y;
			float position_down = _local_pos.z;
			float current_heading = M_PI - _local_pos.heading;

			float sin_heading = sin(current_heading);
			float cos_heading = cos(current_heading);
			float rotated_position_north = cos_heading * position_north - sin_heading * position_east;
			float rotated_position_east = sin_heading * position_north + cos_heading * position_east;

			thrust_setpoint.xyz[0] = pid_calculate(&att_pid, desired_heading, current_heading, rotated_position_north, dt);
			thrust_setpoint.xyz[1] = pid_calculate(&att_pid, desired_heading, current_heading, rotated_position_east, dt);
			thrust_setpoint.xyz[2] = position_down;

			_vehicle_thrust_setpoint_pub.publish(thrust_setpoint);
			// END

			_vehicle_torque_setpoint_pub.publish(torque_setpoint);
			_actuator_controls_status_pub.publish(status);
		}
	}
}

int SeseOmni::task_spawn(int argc, char *argv[])
{
	SeseOmni *instance = new SeseOmni();

	if (instance)
	{
		_object.store(instance);
		_task_id = task_id_is_work_queue;

		if (instance->init())
		{
			return PX4_OK;
		}
	}
	else
	{
		PX4_ERR("alloc failed");
	}

	delete instance;
	_object.store(nullptr);
	_task_id = -1;

	return PX4_ERROR;
}

int SeseOmni::custom_command(int argc, char *argv[])
{
	return print_usage("unknown command");
}

int SeseOmni::print_usage(const char *reason)
{
	if (reason)
	{
		PX4_ERR("%s\n", reason);
	}

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
Rover Differential Drive controller.
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("sese_omni_control", "controller");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return 0;
}

extern "C" __EXPORT int sese_omni_control_main(int argc, char *argv[])
{
	return SeseOmni::main(argc, argv);
}
