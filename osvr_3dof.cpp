/**
* @file 3dof_functions.cpp
*
* @brief OSVR 3DOF Functions
*
* @copyright
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation; either
* version 3.0 of the License, or (at your option) any later version.
*
* @copyright
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* @author Arduino LLC
* @author Adam Munich
*/


//#define OPTION_MEASURE_PERIOD
#define OPTION_SEND_OVER_CDC

#include "./osvr_3dof.h"
#include "./sh2_arduino.h"
#include "../pin_settings.h"

#if !defined(_USING_HID)
	#error "This microprocessor does not have HID support"
#endif

// ----------------------------------------------------------------------------------
// Variables
// ----------------------------------------------------------------------------------

// Sensor object
Hillcrest_ g_hillcrest;

sh2_SensorType_t g_osvr_tracker_sensor_type;

// Constants
const uint8_t g_osvr_tracker_hid_report_length = sizeof(vrpnTrackerReport);

// Event flags	
uint8_t g_vrpn_tracker_use_magnetometer = 0;
uint8_t g_vrpn_tracker_tare_now = 0;
uint8_t g_vrpn_tracker_save_dcd_now = 0;
uint8_t g_vrpn_tracker_enable_dcd_now = 0;
uint8_t g_vrpn_tracker_clear_dcd_now = 0;
uint8_t g_osvr_tracker_measure_frequency_now = 0;

// Sensor orientation
vrpnTracker_int_quaternion_t g_osvr_tracker_sensor_rotation;
vrpnTracker_float_euler_t g_osvr_tracker_rotation_euler;

// Sensor report values
sh2_SensorValue_t g_osvr_tracker_sensor_values;
	
// Sensor report millis
uint8_t g_osvr_tracker_report_period_counter;
uint32_t g_osvr_tracker_report_last;

// ----------------------------------------------------------------------------------
// Function definitions
// ----------------------------------------------------------------------------------			

// ----------------------------------------------------------------------------------
// Public	
static const uint8_t _hidReportDescriptor[] PROGMEM = 	
{		
	0x06, 0xFF, 0xFF,  // Usage Page (Vendor Defined 0xFFFF)
	0x09, 0x01,        // Usage (0x01)
	0xA1, 0x01,        // Collection (Application)
				
	0x09, 0x02,        //   Usage (0x02)
	0x09, 0x03,        //   Usage (0x03)
	0x15, 0x00,        //   Logical Minimum (0)
	0x26, 0xFF, 0x00,  //   Logical Maximum (255)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x10,        //   Report Count (16)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

	0x09, 0x04,        //   Usage (0x04)
	0x09, 0x05,        //   Usage (0x05)
	0x15, 0x00,        //   Logical Minimum (0)
	0x26, 0xFF, 0x00,  //   Logical Maximum (255)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x40,        //   Report Count (64)
	0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)

	0x09, 0x06,        //   Usage (0x06)
	0x09, 0x07,        //   Usage (0x07)
	0x15, 0x00,        //   Logical Minimum (0)
	0x26, 0xFF, 0x00,  //   Logical Maximum (255)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x10,        //   Report Count (16)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0xC0,              // End Collection

	// 53 bytes
};
						 	
vrpnTracker_::vrpnTracker_(void)
{
	static HIDSubDescriptor node(_hidReportDescriptor, sizeof(_hidReportDescriptor));
	HID().AppendDescriptor(&node);
}

uint8_t vrpnTracker_ :: begin(uint32_t pin_rstn, uint32_t pin_bootn, uint32_t pin_intn)
{
	osvr_tracker_hid_report.vrpn_message_sequence_number = 0;
	
	// Sensor pins
	sh2_SensorPins_t osvr_tracker_sensor_pins;
	osvr_tracker_sensor_pins.pin_rstn = pin_rstn;
	osvr_tracker_sensor_pins.pin_bootn = pin_bootn;
	osvr_tracker_sensor_pins.pin_intn = pin_intn;
		
	g_hillcrest.begin(osvr_tracker_sensor_pins);		
}

void vrpnTracker_ :: setup(vrpnTracker_sensorType_t sensor_type, uint16_t reportInterval_us, float prediction_amount_s, float euler_x, float euler_y, float euler_z)
{
	// Sensor type
	// @TODO could pobably done with memcpy or pointers or something...
	switch(sensor_type)
	{
		case vrpnTracker_sensorType::FILTERED_GRAVITY_REFERENCED_ORIENTATION:
		{
			g_osvr_tracker_sensor_type = sh2_sensorType::SH2_TYPE_AR_VR_STABILIZED_GAME_ROTATION_VECTOR;
			break;
		}
		case vrpnTracker_sensorType::FILTERED_ORIENTATION:
		{
			g_osvr_tracker_sensor_type = sh2_sensorType::SH2_TYPE_AR_VR_STABILIZED_ROTATION_VECTOR;		
			break;
		}
		case vrpnTracker_sensorType::SIMPLE_GRAVITY_REFERENCED_ORIENTATION:
		{
			g_osvr_tracker_sensor_type = sh2_sensorType::SH2_TYPE_GAME_ROTATION_VECTOR;
			break;
		}
		case vrpnTracker_sensorType::SIMPLE_GEOMAGNETIC_REFERENCED_ORIENTATION:
		{
			g_osvr_tracker_sensor_type = sh2_sensorType::SH2_TYPE_GEOMAGNETIC_ROTATION_VECTOR;
			break;
		}
		case vrpnTracker_sensorType::SENSOR_FUSION_NO_MAGNETOMETER:
		{
			g_osvr_tracker_sensor_type = sh2_sensorType::SH2_TYPE_GYRO_INTEGRATED_ROTATION_VECTOR_NO_MAG;
			break;
		}
		case vrpnTracker_sensorType::SENSOR_FUSION_WITH_MAGNETOMETER:
		{
			g_osvr_tracker_sensor_type = sh2_sensorType::SH2_TYPE_GYRO_INTEGRATED_ROTATION_VECTOR_WITH_MAG;
			break;
		}
		case vrpnTracker_sensorType::SIMPLE_ORIENTATION:
		{
			g_osvr_tracker_sensor_type = sh2_sensorType::SH2_TYPE_ROTATION_VECTOR;
			break;
		}
	}
	
	// Interrupt config
	sh2_SensorInterrupts_t osvr_tracker_sensor_ints;
	osvr_tracker_sensor_ints.report_interval_microseconds = reportInterval_us;

	// DMP config
	sh2_SensorDMPConfig_t osvr_tracker_sensor_dmp;
	osvr_tracker_sensor_dmp.GIRV_PRED_AMT_S = 0.002;

	// Broken 
	sh2_Euler_t sensorOrientation;

	// Write config to sensor
	g_hillcrest.configure(g_osvr_tracker_sensor_type, osvr_tracker_sensor_ints, osvr_tracker_sensor_dmp, sensorOrientation);

	// Calculate and set rotation vector...
	g_osvr_tracker_rotation_euler.x = euler_x;
	g_osvr_tracker_rotation_euler.y = euler_y;
	g_osvr_tracker_rotation_euler.z = euler_z;
	integer_math_convert_euler(g_osvr_tracker_rotation_euler, &g_osvr_tracker_sensor_rotation, 14);	
}

void vrpnTracker_ :: end(void)
{
	Hillcrest.end();
}
						
size_t vrpnTracker_ :: loopTask(void)
{						
	uint8_t osvr_tracker_sensor_event = g_hillcrest.checkReport(&g_osvr_tracker_sensor_values);
		
	if(osvr_tracker_sensor_event){			
		vrpnTracker_int_quaternion_t sensor_vector;
			
		sensor_vector.q_point = 14;
		sensor_vector.real = vrpn_twosCompliment(14, g_osvr_tracker_sensor_values.un.gameRotationVector.real);
		sensor_vector.i	= vrpn_twosCompliment(14, g_osvr_tracker_sensor_values.un.gameRotationVector.i);
		sensor_vector.j = vrpn_twosCompliment(14, g_osvr_tracker_sensor_values.un.gameRotationVector.j);
		sensor_vector.k = vrpn_twosCompliment(14, g_osvr_tracker_sensor_values.un.gameRotationVector.k);
			
		integer_math_rotate_quaternion(g_osvr_tracker_sensor_rotation, &sensor_vector, 14);
			
		int16_t quat_real_q = sensor_vector.real;
		int16_t quat_i_q = sensor_vector.i;
		int16_t quat_j_q = sensor_vector.j;
		int16_t quat_k_q = sensor_vector.k;			 
								
		int16_t ang_vel_x = vrpn_deciDegreesToRadiansTwosComplimentQ14(g_osvr_tracker_sensor_values.un.gyroIntegratedRV.angVelX);
		int16_t ang_vel_y = vrpn_deciDegreesToRadiansTwosComplimentQ14(g_osvr_tracker_sensor_values.un.gyroIntegratedRV.angVelY);
		int16_t ang_vel_z = vrpn_deciDegreesToRadiansTwosComplimentQ14(g_osvr_tracker_sensor_values.un.gyroIntegratedRV.angVelZ);
				
		osvr_tracker_hid_report.vrpn_report_version					= 0x02;
		osvr_tracker_hid_report.vrpn_message_sequence_number++;
		osvr_tracker_hid_report.vrpn_quaternion_i_lsb				= lowByte(quat_i_q);
		osvr_tracker_hid_report.vrpn_quaternion_i_msb				= highByte(quat_i_q);
		osvr_tracker_hid_report.vrpn_quaternion_j_lsb				= lowByte(quat_j_q);
		osvr_tracker_hid_report.vrpn_quaternion_j_msb				= highByte(quat_j_q);
		osvr_tracker_hid_report.vrpn_quaternion_k_lsb				= lowByte(quat_k_q);
		osvr_tracker_hid_report.vrpn_quaternion_k_msb				= highByte(quat_k_q);
		osvr_tracker_hid_report.vrpn_quaternion_real_lsb			= lowByte(quat_real_q);
		osvr_tracker_hid_report.vrpn_quaternion_real_msb			= highByte(quat_real_q);
		
		osvr_tracker_hid_report.vrpn_angular_velocity_about_x_lsb	= lowByte(ang_vel_x);
		osvr_tracker_hid_report.vrpn_angular_velocity_about_x_msb	= highByte(ang_vel_x);
		osvr_tracker_hid_report.vrpn_angular_velocity_about_y_lsb	= lowByte(ang_vel_y);
		osvr_tracker_hid_report.vrpn_angular_velocity_about_y_msb	= highByte(ang_vel_y);
		osvr_tracker_hid_report.vrpn_angular_velocity_about_z_lsb	= lowByte(ang_vel_z);
		osvr_tracker_hid_report.vrpn_angular_velocity_about_z_msb	= highByte(ang_vel_z);
	
		sendHIDReport(&osvr_tracker_hid_report);
		
		/* IMU Serial USB */
		#ifdef OPTION_SEND_OVER_CDC
		SerialUSB.write(osvr_tracker_hid_report.vrpn_message_sequence_number);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_i_lsb);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_i_msb);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_j_lsb);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_j_msb);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_k_lsb);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_k_msb);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_real_lsb);
		SerialUSB.write(osvr_tracker_hid_report.vrpn_quaternion_real_msb);
		SerialUSB.print("\r\n");		
		#endif
		
		#ifdef OPTION_MEASURE_PERIOD
			uint32_t osvr_tracker_report_now = micros();		
			uint32_t osvr_tracker_report_period = osvr_tracker_report_now - g_osvr_tracker_report_last;
			g_osvr_tracker_report_last = osvr_tracker_report_now;
			g_osvr_tracker_report_period_counter ++;
			
			if(g_osvr_tracker_report_period_counter == 100)
			{
				g_osvr_tracker_report_period_counter = 0;
				SerialUSB.println(osvr_tracker_report_period);	
			}
		#endif
	}
	
	if(g_vrpn_tracker_tare_now)
	{
		sh2_TareBasis_t basis = SH2_TARE_BASIS_ROTATION_VECTOR;
		g_hillcrest.tare(true, true, true, basis, false);
		g_vrpn_tracker_tare_now = false;
	}
	
	else if(g_vrpn_tracker_save_dcd_now)
	{
		g_hillcrest.saveDCD();
		g_vrpn_tracker_save_dcd_now = false;
	}
	
	else if(g_vrpn_tracker_enable_dcd_now)
	{
		g_hillcrest.setDCD(true, true, true);
		g_vrpn_tracker_enable_dcd_now = false;
	}
	
	else if(g_vrpn_tracker_clear_dcd_now)
	{
		g_hillcrest.clearDCD();
		g_vrpn_tracker_clear_dcd_now = false;
	}	
		
	return osvr_tracker_sensor_event;
}


// ----------------------------------------------------------------------------------
// Private
void vrpnTracker_ :: sendHIDReport(vrpnTrackerReport* report)
{
	// Endpoint 0x04
	USBDevice.send(0x04, report, g_osvr_tracker_hid_report_length);
}

void vrpnTracker_ :: integer_math_convert_euler(vrpnTracker_float_euler_t euler, vrpnTracker_int_quaternion_t * quaterinon, int8_t q_point)
{
	vrpnTracker_float_euler_t radians;
	
	radians.x = euler.x * vrpn_degressToRadiansScalar;
	radians.y = euler.y * vrpn_degressToRadiansScalar;
	radians.z = euler.z * vrpn_degressToRadiansScalar;
	
	// Assuming the angles are in radians.
	float c1 = cosf(radians.y);
	float s1 = sinf(radians.y);
	
	float c2 = cosf(radians.z);
	float s2 = sinf(radians.z);
	
	float c3 = cosf(radians.x);
	float s3 = sinf(radians.x);
	
	float real = sqrtf(1.0 + c1 * c2 + c1*c3 - s1 * s2 * s3 + c2*c3) / 2.0;
	float r4 = real * 4.0;
	
	quaterinon -> real = vrpn_twosCompliment(q_point, real);
	
	quaterinon -> i = vrpn_twosCompliment(q_point, (float)((c2 * s3 + c1 * s3 + s1 * s2 * c3) / r4));
	quaterinon -> j = vrpn_twosCompliment(q_point, (float)((s1 * c2 + s1 * c3 + c1 * s2 * s3) / r4));
	quaterinon -> k = vrpn_twosCompliment(q_point, (float)((-s1 * s3 + c1 * s2 * c3 + s2) / r4));
	
	quaterinon -> q_point = q_point;
}

void vrpnTracker_ :: integer_math_rotate_quaternion(vrpnTracker_int_quaternion_t rotationVector, vrpnTracker_int_quaternion_t * quaternion, int8_t q_point)
{
	int32_t quaternion_real =
	((quaternion -> real * rotationVector.real) >> q_point)	-
	((quaternion -> i * rotationVector.i) >> q_point) -
	((quaternion -> j * rotationVector.j) >> q_point) -
	((quaternion -> k * rotationVector.k) >> q_point);  // 1
	
	int32_t quaternion_i =
	((quaternion -> real * rotationVector.i) >> q_point) +
	((quaternion -> i * rotationVector.real) >> q_point) +
	((quaternion -> j * rotationVector.k) >> q_point) -
	((quaternion -> k * rotationVector.j) >> q_point);  // i
	
	int32_t quaternion_j =
	((quaternion -> real * rotationVector.j) >> q_point) -
	((quaternion -> i * rotationVector.k) >> q_point) +
	((quaternion -> j * rotationVector.real) >> q_point) +
	((quaternion -> k * rotationVector.i) >> q_point);  // j
	
	int32_t quaternion_k =
	((quaternion -> real * rotationVector.k) >> q_point) +
	((quaternion -> i * rotationVector.j) >> q_point) -
	((quaternion -> j * rotationVector.i) >> q_point) +
	((quaternion -> k * rotationVector.real) >> q_point);  // k
	
	quaternion -> real = quaternion_real;
	
	quaternion -> i = quaternion_i;
	quaternion -> j = quaternion_j;
	quaternion -> k = quaternion_k;
	
	quaternion -> q_point = q_point;
}


	
vrpnTracker_ vrpn_tracker;