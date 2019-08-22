// Most of the functionality of this library is based on the VL53L1X API
// provided by ST (STSW-IMG007), and some of the explanatory comments are quoted
// or paraphrased from the API source code, API user manual (UM2356), and
// VL53L1X datasheet.

#include <stdbool.h>
#include "vl53l1x.h"
#include <applibs/i2c.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <applibs/log.h>

int vl53l1x_i2cFd = -1;
const uint8_t AddressDefault = 0b0101001;
const uint32_t TimingGuard = 4528;
const uint16_t TargetRate = 0x0A00;

// Constructors ////////////////////////////////////////////////////////////////

//VL53L1X_VL53L1X()
//	: address(AddressDefault)
//	, io_timeout(0) // no timeout
//	, did_timeout(false)
//	, calibrated(false)
//	, saved_vhv_init(0)
//	, saved_vhv_timeout(0)
//	, distance_mode(Unknown)
//{
//}

void delayMicroseconds(long i) {
	struct timespec trem;
	nanosleep((const struct timespec[]) {{ 0, i * 1000 }}, &trem);
}

void delay(long i) {
	delayMicroseconds(i * 1000000);
}

// Public Methods //////////////////////////////////////////////////////////////

void VL53L1X_startTimeout(void) { clock_gettime(CLOCK_REALTIME, &timeout_start_ms); }
bool VL53L1X_checkTimeoutExpired(void) { struct timespec time; clock_gettime(CLOCK_REALTIME, &time); return (io_timeout > 0) && ((long)((time.tv_nsec / 1000) - (timeout_start_ms.tv_nsec / 1000)) > io_timeout); }
float VL53L1X_countRateFixedToFloat(uint16_t count_rate_fixed) { return (float)count_rate_fixed / (1 << 7); }
uint8_t VL53L1X_getAddress(void) { return address; }
DistanceMode VL53L1X_getDistanceMode(void) { return distance_mode; }
uint16_t readRangeContinuousMillimeters(bool blocking) { return VL53L1X_read(blocking); } // alias of read(void)
bool dataReady(void) { return (VL53L1X_readReg(GPIO__TIO_HV_STATUS) & 0x01) == 0; }
void vl53l1x_setTimeout(uint16_t timeout) { io_timeout = timeout; }
uint16_t vl53l1x_getTimeout(void) { return io_timeout; }

void VL53L1X_setAddress(uint8_t new_addr)
{
	VL53L1X_writeReg(I2C_SLAVE__DEVICE_ADDRESS, new_addr & 0x7F);
	address = new_addr;
}

// Initialize sensor using settings taken mostly from VL53L1_DataInit() and
// VL53L1_StaticInit().
// If io_2v8 (optional) is true or not given, the sensor is configured for 2V8
// mode.
bool VL53L1X_init(bool io_2v8, int ISU)
{
	address = AddressDefault;
	vl53l1x_i2cFd = I2CMaster_Open(ISU);
	if (vl53l1x_i2cFd == -1) return false;
	I2CMaster_SetBusSpeed(vl53l1x_i2cFd, I2C_BUS_SPEED_FAST);

	// check model ID and module type registers (values specified in datasheet)
	if (VL53L1X_readReg16Bit(IDENTIFICATION__MODEL_ID) != 0xEACC) { return false; }

	// VL53L1_software_reset() begin

	VL53L1X_writeReg(SOFT_RESET, 0x00);
	//nanosleep(0, 0);
	delayMicroseconds(100);
	VL53L1X_writeReg(SOFT_RESET, 0x01);

	// give it some time to boot; otherwise the sensor NACKs during the readReg()
	// call below and the Arduino 101 doesn't seem to handle that well
	delay(1);
	//nanosleep(1, 0);

	// VL53L1_poll_for_boot_completion() begin

	VL53L1X_startTimeout();

	// check last_status in case we still get a NACK to try to deal with it correctly
	while ((VL53L1X_readReg(FIRMWARE__SYSTEM_STATUS) & 0x01) == 0 || last_status != 0)
	{
		if (VL53L1X_checkTimeoutExpired())
		{
			did_timeout = true;
			return false;
		}
	}
	// VL53L1_poll_for_boot_completion() end

	// VL53L1_software_reset() end

	// VL53L1_DataInit() begin

	// sensor uses 1V8 mode for I/O by default; switch to 2V8 mode if necessary
	if (io_2v8)
	{
		VL53L1X_writeReg(PAD_I2C_HV__EXTSUP_CONFIG,
			VL53L1X_readReg(PAD_I2C_HV__EXTSUP_CONFIG) | 0x01);
	}

	// store oscillator info for later use
	fast_osc_frequency = VL53L1X_readReg16Bit(OSC_MEASURED__FAST_OSC__FREQUENCY);
	osc_calibrate_val = VL53L1X_readReg16Bit(RESULT__OSC_CALIBRATE_VAL);

	// VL53L1_DataInit() end

	// VL53L1_StaticInit() begin

	// Note that the API does not actually apply the configuration settings below
	// when VL53L1_StaticInit() is called: it keeps a copy of the sensor's
	// register contents in memory and doesn't actually write them until a
	// measurement is started. Writing the configuration here means we don't have
	// to keep it all in memory and avoids a lot of redundant writes later.

	// the API sets the preset mode to LOWPOWER_AUTONOMOUS here:
	// VL53L1_set_preset_mode() begin

	// VL53L1_preset_mode_standard_ranging() begin

	// values labeled "tuning parm default" are from vl53l1_tuning_parm_defaults.h
	// (API uses these in VL53L1_init_tuning_parm_storage_struct())

	// static config
	// API resets PAD_I2C_HV__EXTSUP_CONFIG here, but maybe we don't want to do
	// that? (seems like it would disable 2V8 mode)
	VL53L1X_writeReg16Bit(DSS_CONFIG__TARGET_TOTAL_RATE_MCPS, TargetRate); // should already be this value after reset
	VL53L1X_writeReg(GPIO__TIO_HV_STATUS, 0x02);
	VL53L1X_writeReg(SIGMA_ESTIMATOR__EFFECTIVE_PULSE_WIDTH_NS, 8); // tuning parm default
	VL53L1X_writeReg(SIGMA_ESTIMATOR__EFFECTIVE_AMBIENT_WIDTH_NS, 16); // tuning parm default
	VL53L1X_writeReg(ALGO__CROSSTALK_COMPENSATION_VALID_HEIGHT_MM, 0x01);
	VL53L1X_writeReg(ALGO__RANGE_IGNORE_VALID_HEIGHT_MM, 0xFF);
	VL53L1X_writeReg(ALGO__RANGE_MIN_CLIP, 0); // tuning parm default
	VL53L1X_writeReg(ALGO__CONSISTENCY_CHECK__TOLERANCE, 2); // tuning parm default

	// general config
	VL53L1X_writeReg16Bit(SYSTEM__THRESH_RATE_HIGH, 0x0000);
	VL53L1X_writeReg16Bit(SYSTEM__THRESH_RATE_LOW, 0x0000);
	VL53L1X_writeReg(DSS_CONFIG__APERTURE_ATTENUATION, 0x38);

	// timing config
	// most of these settings will be determined later by distance and timing
	// budget configuration
	VL53L1X_writeReg16Bit(RANGE_CONFIG__SIGMA_THRESH, 360); // tuning parm default
	VL53L1X_writeReg16Bit(RANGE_CONFIG__MIN_COUNT_RATE_RTN_LIMIT_MCPS, 192); // tuning parm default

	// dynamic config

	VL53L1X_writeReg(SYSTEM__GROUPED_PARAMETER_HOLD_0, 0x01);
	VL53L1X_writeReg(SYSTEM__GROUPED_PARAMETER_HOLD_1, 0x01);
	VL53L1X_writeReg(SD_CONFIG__QUANTIFIER, 2); // tuning parm default

	// VL53L1_preset_mode_standard_ranging() end

	// from VL53L1_preset_mode_timed_ranging_*
	// GPH is 0 after reset, but writing GPH0 and GPH1 above seem to set GPH to 1,
	// and things don't seem to work if we don't set GPH back to 0 (which the API
	// does here).
	VL53L1X_writeReg(SYSTEM__GROUPED_PARAMETER_HOLD, 0x00);
	VL53L1X_writeReg(SYSTEM__SEED_CONFIG, 1); // tuning parm default

	// from VL53L1_config_low_power_auto_mode
	VL53L1X_writeReg(SYSTEM__SEQUENCE_CONFIG, 0x8B); // VHV, PHASECAL, DSS1, RANGE
	VL53L1X_writeReg16Bit(DSS_CONFIG__MANUAL_EFFECTIVE_SPADS_SELECT, 200 << 8);
	VL53L1X_writeReg(DSS_CONFIG__ROI_MODE_CONTROL, 2); // REQUESTED_EFFFECTIVE_SPADS

	// VL53L1_set_preset_mode() end

	// default to long range, 50 ms timing budget
	// note that this is different than what the API defaults to
	VL53L1X_setDistanceMode(Long);
	VL53L1X_setMeasurementTimingBudget(50000);

	// VL53L1_StaticInit() end

	// the API triggers this change in VL53L1_init_and_start_range() once a
	// measurement is started; assumes MM1 and MM2 are disabled
	VL53L1X_writeReg16Bit(ALGO__PART_TO_PART_RANGE_OFFSET_MM,
		VL53L1X_readReg16Bit(MM_CONFIG__OUTER_OFFSET_MM) * 4);

	// Do calibration 
	//VL53L1X_setupManualCalibration();
	//calibrated = true;

	return true;
}

// Write an 8-bit register
void VL53L1X_writeReg(uint16_t reg, uint8_t value)
{
	//Wire.beginTransmission(address);
	//Wire.write((reg >> 8) & 0xFF); // reg high byte
	//Wire.write(reg & 0xFF); // reg low byte
	//Wire.write(value);
	//last_status = Wire.endTransmission();

	uint8_t buff[3] = { (reg >> 8) & 0xFF, (reg & 0xFF), value };
	int result = I2CMaster_Write(vl53l1x_i2cFd, address, buff, 3);

}

// Write a 16-bit register
void VL53L1X_writeReg16Bit(uint16_t reg, uint16_t value)
{
	//Wire.beginTransmission(address);
	//Wire.write((reg >> 8) & 0xFF); // reg high byte
	//Wire.write(reg & 0xFF); // reg low byte
	//Wire.write((value >> 8) & 0xFF); // value high byte
	//Wire.write(value & 0xFF); // value low byte
	//last_status = Wire.endTransmission();

	uint8_t buff[4] = { (reg >> 8) & 0xFF, (reg & 0xFF), (value >> 8) & 0xFF, value & 0xFF };
	int result = I2CMaster_Write(vl53l1x_i2cFd, address, buff, 4);
}

// Write a 32-bit register
void VL53L1X_writeReg32Bit(uint16_t reg, uint32_t value)
{
	//Wire.beginTransmission(address);
	//Wire.write((reg >> 8) & 0xFF); // reg high byte
	//Wire.write(reg & 0xFF); // reg low byte
	//Wire.write((value >> 24) & 0xFF); // value highest byte
	//Wire.write((value >> 16) & 0xFF);
	//Wire.write((value >> 8) & 0xFF);
	//Wire.write(value & 0xFF); // value lowest byte
	//last_status = Wire.endTransmission();

	uint8_t buff[6] = { (reg >> 8) & 0xFF, (reg & 0xFF), (value >> 24) & 0xFF,(value >> 16) & 0xFF,(value >> 8) & 0xFF, value & 0xFF };
	I2CMaster_Write(vl53l1x_i2cFd, address, buff, 6);
}

// Read an 8-bit register
uint8_t VL53L1X_readReg(regAddr reg)
{
	//uint8_t value;

	//Wire.beginTransmission(address);
	//Wire.write((reg >> 8) & 0xFF); // reg high byte
	//Wire.write(reg & 0xFF); // reg low byte
	//last_status = Wire.endTransmission();

	uint8_t buff[2] = { (reg >> 8) & 0xFF , reg & 0xFF};
	int result = I2CMaster_Write(vl53l1x_i2cFd, address, buff, 2);

	uint8_t value[1];
	I2CMaster_Read(vl53l1x_i2cFd, address, value, 1);

	//Wire.requestFrom(address, (uint8_t)1);
	//value = Wire.read();

	return value[0];
}

// Read a 16-bit register
uint16_t VL53L1X_readReg16Bit(uint16_t reg)
{
	uint16_t value;
	uint8_t values[2];

	//Wire.beginTransmission(address);
	//Wire.write((reg >> 8) & 0xFF); // reg high byte
	//Wire.write(reg & 0xFF); // reg low byte
	//last_status = Wire.endTransmission();

	uint8_t buff[2] = { (reg >> 8) & 0xFF , reg & 0xFF };
	int result = I2CMaster_Write(vl53l1x_i2cFd, address, buff, 2);

	if (result == -1)
	{
		Log_Debug("ERROR: Write: errno=%d (%s)\n", errno, strerror(errno));
	}

	//Wire.requestFrom(address, (uint8_t)2);
	//value = (uint16_t)Wire.read() << 8; // value high byte
	//value |= Wire.read();      // value low byte

	I2CMaster_Read(vl53l1x_i2cFd, address, values, 2);
	value = (uint16_t) values[0] << 8;
	value |= values[1];

	return value;
}

// Read a 32-bit register
uint32_t VL53L1X_readReg32Bit(uint16_t reg)
{
	uint32_t value = 0;
	uint8_t values[4];

	//Wire.beginTransmission(address);
	//Wire.write((reg >> 8) & 0xFF); // reg high byte
	//Wire.write(reg & 0xFF); // reg low byte
	//last_status = Wire.endTransmission();

	uint8_t buff[2] = { (reg >> 8) & 0xFF , reg & 0xFF };
	int result = I2CMaster_Write(vl53l1x_i2cFd, address, buff, 2);

	//Wire.requestFrom(address, (uint8_t)4);
	//value = (uint32_t)Wire.read() << 24; // value highest byte
	//value |= (uint32_t)Wire.read() << 16;
	//value |= (uint16_t)Wire.read() << 8;
	//value |= Wire.read();       // value lowest byte

	I2CMaster_Read(vl53l1x_i2cFd, address, values, 4);
	value |= (uint32_t)values[0] << 24;
	value |= (uint32_t)values[1] << 16;
	value |= (uint16_t)values[2] << 8;
	value |= values[3];

	return value;
}

// set distance mode to Short, Medium, or Long
// based on VL53L1_SetDistanceMode()
bool VL53L1X_setDistanceMode(DistanceMode mode)
{
	// save existing timing budget
	uint32_t budget_us = VL53L1X_getMeasurementTimingBudget();

	switch (mode)
	{
	case Short:
		// from VL53L1_preset_mode_standard_ranging_short_range()

		// timing config
		VL53L1X_writeReg(RANGE_CONFIG__VCSEL_PERIOD_A, 0x07);
		VL53L1X_writeReg(RANGE_CONFIG__VCSEL_PERIOD_B, 0x05);
		VL53L1X_writeReg(RANGE_CONFIG__VALID_PHASE_HIGH, 0x38);

		// dynamic config
		VL53L1X_writeReg(SD_CONFIG__WOI_SD0, 0x07);
		VL53L1X_writeReg(SD_CONFIG__WOI_SD1, 0x05);
		VL53L1X_writeReg(SD_CONFIG__INITIAL_PHASE_SD0, 6); // tuning parm default
		VL53L1X_writeReg(SD_CONFIG__INITIAL_PHASE_SD1, 6); // tuning parm default

		break;

	case Medium:
		// from VL53L1_preset_mode_standard_ranging()

		// timing config
		VL53L1X_writeReg(RANGE_CONFIG__VCSEL_PERIOD_A, 0x0B);
		VL53L1X_writeReg(RANGE_CONFIG__VCSEL_PERIOD_B, 0x09);
		VL53L1X_writeReg(RANGE_CONFIG__VALID_PHASE_HIGH, 0x78);

		// dynamic config
		VL53L1X_writeReg(SD_CONFIG__WOI_SD0, 0x0B);
		VL53L1X_writeReg(SD_CONFIG__WOI_SD1, 0x09);
		VL53L1X_writeReg(SD_CONFIG__INITIAL_PHASE_SD0, 10); // tuning parm default
		VL53L1X_writeReg(SD_CONFIG__INITIAL_PHASE_SD1, 10); // tuning parm default

		break;

	case Long: // long
	  // from VL53L1_preset_mode_standard_ranging_long_range()

	  // timing config
		VL53L1X_writeReg(RANGE_CONFIG__VCSEL_PERIOD_A, 0x0F);
		VL53L1X_writeReg(RANGE_CONFIG__VCSEL_PERIOD_B, 0x0D);
		VL53L1X_writeReg(RANGE_CONFIG__VALID_PHASE_HIGH, 0xB8);

		// dynamic config
		VL53L1X_writeReg(SD_CONFIG__WOI_SD0, 0x0F);
		VL53L1X_writeReg(SD_CONFIG__WOI_SD1, 0x0D);
		VL53L1X_writeReg(SD_CONFIG__INITIAL_PHASE_SD0, 14); // tuning parm default
		VL53L1X_writeReg(SD_CONFIG__INITIAL_PHASE_SD1, 14); // tuning parm default

		break;

	default:
		// unrecognized mode - do nothing
		return false;
	}

	// reapply timing budget
	VL53L1X_setMeasurementTimingBudget(budget_us);

	// save mode so it can be returned by getDistanceMode()
	distance_mode = mode;

	return true;
}

// Set the measurement timing budget in microseconds, which is the time allowed
// for one measurement. A longer timing budget allows for more accurate
// measurements.
// based on VL53L1_SetMeasurementTimingBudgetMicroSeconds()
bool VL53L1X_setMeasurementTimingBudget(uint32_t budget_us)
{
	// assumes PresetMode is LOWPOWER_AUTONOMOUS

	if (budget_us <= TimingGuard) { return false; }

	uint32_t range_config_timeout_us = budget_us -= TimingGuard;
	if (range_config_timeout_us > 1100000) { return false; } // FDA_MAX_TIMING_BUDGET_US * 2

	range_config_timeout_us /= 2;

	// VL53L1_calc_timeout_register_values() begin

	uint32_t macro_period_us;

	// "Update Macro Period for Range A VCSEL Period"
	macro_period_us = VL53L1X_calcMacroPeriod(VL53L1X_readReg(RANGE_CONFIG__VCSEL_PERIOD_A));

	// "Update Phase timeout - uses Timing A"
	// Timeout of 1000 is tuning parm default (TIMED_PHASECAL_CONFIG_TIMEOUT_US_DEFAULT)
	// via VL53L1_get_preset_mode_timing_cfg().
	uint32_t phasecal_timeout_mclks = VL53L1X_timeoutMicrosecondsToMclks(1000, macro_period_us);
	if (phasecal_timeout_mclks > 0xFF) { phasecal_timeout_mclks = 0xFF; }
	VL53L1X_writeReg(PHASECAL_CONFIG__TIMEOUT_MACROP, phasecal_timeout_mclks);

	// "Update MM Timing A timeout"
	// Timeout of 1 is tuning parm default (LOWPOWERAUTO_MM_CONFIG_TIMEOUT_US_DEFAULT)
	// via VL53L1_get_preset_mode_timing_cfg(). With the API, the register
	// actually ends up with a slightly different value because it gets assigned,
	// retrieved, recalculated with a different macro period, and reassigned,
	// but it probably doesn't matter because it seems like the MM ("mode
	// mitigation"?) sequence steps are disabled in low power auto mode anyway.
	VL53L1X_writeReg16Bit(MM_CONFIG__TIMEOUT_MACROP_A, VL53L1X_encodeTimeout(
		VL53L1X_timeoutMicrosecondsToMclks(1, macro_period_us)));

	// "Update Range Timing A timeout"
	VL53L1X_writeReg16Bit(RANGE_CONFIG__TIMEOUT_MACROP_A, VL53L1X_encodeTimeout(
		VL53L1X_timeoutMicrosecondsToMclks(range_config_timeout_us, macro_period_us)));

	// "Update Macro Period for Range B VCSEL Period"
	macro_period_us = VL53L1X_calcMacroPeriod(VL53L1X_readReg(RANGE_CONFIG__VCSEL_PERIOD_B));

	// "Update MM Timing B timeout"
	// (See earlier comment about MM Timing A timeout.)
	VL53L1X_writeReg16Bit(MM_CONFIG__TIMEOUT_MACROP_B, VL53L1X_encodeTimeout(
		VL53L1X_timeoutMicrosecondsToMclks(1, macro_period_us)));

	// "Update Range Timing B timeout"
	VL53L1X_writeReg16Bit(RANGE_CONFIG__TIMEOUT_MACROP_B, VL53L1X_encodeTimeout(
		VL53L1X_timeoutMicrosecondsToMclks(range_config_timeout_us, macro_period_us)));

	// VL53L1_calc_timeout_register_values() end

	return true;
}

// Get the measurement timing budget in microseconds
// based on VL53L1_SetMeasurementTimingBudgetMicroSeconds()
uint32_t VL53L1X_getMeasurementTimingBudget(void)
{
	// assumes PresetMode is LOWPOWER_AUTONOMOUS and these sequence steps are
	// enabled: VHV, PHASECAL, DSS1, RANGE

	// VL53L1_get_timeouts_us() begin

	// "Update Macro Period for Range A VCSEL Period"
	uint32_t macro_period_us = VL53L1X_calcMacroPeriod(VL53L1X_readReg(RANGE_CONFIG__VCSEL_PERIOD_A));

	// "Get Range Timing A timeout"

	uint32_t range_config_timeout_us = VL53L1X_timeoutMclksToMicroseconds(VL53L1X_decodeTimeout(
		VL53L1X_readReg16Bit(RANGE_CONFIG__TIMEOUT_MACROP_A)), macro_period_us);

	// VL53L1_get_timeouts_us() end

	return  2 * range_config_timeout_us + TimingGuard;
}

// Start continuous ranging measurements, with the given inter-measurement
// period in milliseconds determining how often the sensor takes a measurement.
void VL53L1X_startContinuous(uint32_t period_ms)
{
	// from VL53L1_set_inter_measurement_period_ms()
	VL53L1X_writeReg32Bit(SYSTEM__INTERMEASUREMENT_PERIOD, period_ms * osc_calibrate_val);

	VL53L1X_writeReg(SYSTEM__INTERRUPT_CLEAR, 0x01); // sys_interrupt_clear_range
	VL53L1X_writeReg(SYSTEM__MODE_START, 0x40); // mode_range__timed
}

// Stop continuous measurements
// based on VL53L1_stop_range()
void VL53L1X_stopContinuous(void)
{
	VL53L1X_writeReg(SYSTEM__MODE_START, 0x80); // mode_range__abort

	// VL53L1_low_power_auto_data_stop_range() begin

	calibrated = false;

	// "restore vhv configs"
	if (saved_vhv_init != 0)
	{
		VL53L1X_writeReg(VHV_CONFIG__INIT, saved_vhv_init);
	}
	if (saved_vhv_timeout != 0)
	{
		VL53L1X_writeReg(VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND, saved_vhv_timeout);
	}

	// "remove phasecal override"
	VL53L1X_writeReg(PHASECAL_CONFIG__OVERRIDE, 0x00);

	// VL53L1_low_power_auto_data_stop_range() end
}

// Returns a range reading in millimeters when continuous mode is active
// (readRangeSingleMillimeters() also calls this function after starting a
// single-shot range measurement)
uint16_t VL53L1X_read(bool blocking)
{
	if (blocking)
	{
		VL53L1X_startTimeout();
		while (!dataReady())
		{
			if (VL53L1X_checkTimeoutExpired())
			{
				did_timeout = true;
				ranging_data.range_status = None;
				ranging_data.range_mm = 0;
				ranging_data.peak_signal_count_rate_MCPS = 0;
				ranging_data.ambient_count_rate_MCPS = 0;
				return ranging_data.range_mm;
			}
		}
	}

	VL53L1X_readResults();

	if (!calibrated)
	{
		VL53L1X_setupManualCalibration();
		calibrated = true;
	}

	VL53L1X_updateDSS();

	VL53L1X_getRangingData();

	VL53L1X_writeReg(SYSTEM__INTERRUPT_CLEAR, 0x01); // sys_interrupt_clear_range

	return ranging_data.range_mm;
}

// convert a RangeStatus to a readable string
// Note that on an AVR, these strings are stored in RAM (dynamic memory), which
// makes working with them easier but uses up 200+ bytes of RAM (many AVR-based
// Arduinos only have about 2000 bytes of RAM). You can avoid this memory usage
// if you do not call this function in your sketch.
const char* VL53L1X_rangeStatusToString(RangeStatus status)
{
	switch (status)
	{
	case RangeValid:
		return "range valid";

	case SigmaFail:
		return "sigma fail";

	case SignalFail:
		return "signal fail";

	case RangeValidMinRangeClipped:
		return "range valid, min range clipped";

	case OutOfBoundsFail:
		return "out of bounds fail";

	case HardwareFail:
		return "hardware fail";

	case RangeValidNoWrapCheckFail:
		return "range valid, no wrap check fail";

	case WrapTargetFail:
		return "wrap target fail";

	case XtalkSignalFail:
		return "xtalk signal fail";

	case SynchronizationInt:
		return "synchronization int";

	case MinRangeFail:
		return "min range fail";

	case None:
		return "no update";

	default:
		return "unknown status";
	}
}

// Did a timeout occur in one of the read functions since the last call to
// timeoutOccurred()?
bool VL53L1X_timeoutOccurred(void)
{
	bool tmp = did_timeout;
	did_timeout = false;
	return tmp;
}

// Private Methods /////////////////////////////////////////////////////////////

// "Setup ranges after the first one in low power auto mode by turning off
// FW calibration steps and programming static values"
// based on VL53L1_low_power_auto_setup_manual_calibration()
void VL53L1X_setupManualCalibration(void)
{
	// "save original vhv configs"
	saved_vhv_init = VL53L1X_readReg(VHV_CONFIG__INIT);
	saved_vhv_timeout = VL53L1X_readReg(VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND);

	// "disable VHV init"
	VL53L1X_writeReg(VHV_CONFIG__INIT, saved_vhv_init & 0x7F);

	// "set loop bound to tuning param"
	VL53L1X_writeReg(VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND,
		(saved_vhv_timeout & 0x03) + (3 << 2)); // tuning parm default (LOWPOWERAUTO_VHV_LOOP_BOUND_DEFAULT)

	  // "override phasecal"
	VL53L1X_writeReg(PHASECAL_CONFIG__OVERRIDE, 0x01);
	VL53L1X_writeReg(CAL_CONFIG__VCSEL_START, VL53L1X_readReg(PHASECAL_RESULT__VCSEL_START));
}

// read measurement results into buffer
void VL53L1X_readResults(void)
{
	uint8_t dummy;

	//Wire.beginTransmission(address);
	//Wire.write((RESULT__RANGE_STATUS >> 8) & 0xFF); // reg high byte
	//Wire.write(RESULT__RANGE_STATUS & 0xFF); // reg low byte
	//last_status = Wire.endTransmission();

	uint8_t data[2] = { (RESULT__RANGE_STATUS >> 8) & 0xFF ,RESULT__RANGE_STATUS & 0xFF };
	I2CMaster_Write(vl53l1x_i2cFd, address, data, 2);

	//Wire.requestFrom(address, (uint8_t)17);
	//results.range_status = Wire.read();

	I2CMaster_Read(vl53l1x_i2cFd, address, &results.range_status, 1);

	//Wire.read(); // report_status: not used
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);

	//results.stream_count = Wire.read();

	I2CMaster_Read(vl53l1x_i2cFd, address, &results.stream_count, 1);

	//results.dss_actual_effective_spads_sd0 = (uint16_t)Wire.read() << 8; // high byte
	//results.dss_actual_effective_spads_sd0 |= Wire.read();      // low byte

	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.dss_actual_effective_spads_sd0 = (uint16_t) dummy << 8; // high byte
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.dss_actual_effective_spads_sd0 |= dummy;    // low byte
	
	//Wire.read(); // peak_signal_count_rate_mcps_sd0: not used
	//Wire.read();

	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);

	//results.ambient_count_rate_mcps_sd0 = (uint16_t)Wire.read() << 8; // high byte
	//results.ambient_count_rate_mcps_sd0 |= Wire.read();      // low byte

	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.ambient_count_rate_mcps_sd0 = (uint16_t) dummy << 8; // high byte
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.ambient_count_rate_mcps_sd0 |= dummy;      // low byte

	//Wire.read(); // sigma_sd0: not used
	//Wire.read();

	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);

	//Wire.read(); // phase_sd0: not used
	//Wire.read();

	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);

	//results.final_crosstalk_corrected_range_mm_sd0 = (uint16_t)Wire.read() << 8; // high byte
	//results.final_crosstalk_corrected_range_mm_sd0 |= Wire.read();      // low byte

	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.final_crosstalk_corrected_range_mm_sd0 = (uint16_t)dummy << 8; // high byte
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.final_crosstalk_corrected_range_mm_sd0 |= dummy;      // low byte

	//results.peak_signal_count_rate_crosstalk_corrected_mcps_sd0 = (uint16_t)Wire.read() << 8; // high byte
	//results.peak_signal_count_rate_crosstalk_corrected_mcps_sd0 |= Wire.read();      // low byte

	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.peak_signal_count_rate_crosstalk_corrected_mcps_sd0 = (uint16_t)dummy<< 8; // high byte
	I2CMaster_Read(vl53l1x_i2cFd, address, &dummy, 1);
	results.peak_signal_count_rate_crosstalk_corrected_mcps_sd0 |= dummy;      // low byte
}

// perform Dynamic SPAD Selection calculation/update
// based on VL53L1_low_power_auto_update_DSS()
void VL53L1X_updateDSS(void)
{
	uint16_t spadCount = results.dss_actual_effective_spads_sd0;

	if (spadCount != 0)
	{
		// "Calc total rate per spad"

		uint32_t totalRatePerSpad =
			(uint32_t)results.peak_signal_count_rate_crosstalk_corrected_mcps_sd0 +
			results.ambient_count_rate_mcps_sd0;

		// "clip to 16 bits"
		if (totalRatePerSpad > 0xFFFF) { totalRatePerSpad = 0xFFFF; }

		// "shift up to take advantage of 32 bits"
		totalRatePerSpad <<= 16;

		totalRatePerSpad /= spadCount;

		if (totalRatePerSpad != 0)
		{
			// "get the target rate and shift up by 16"
			uint32_t requiredSpads = ((uint32_t)TargetRate << 16) / totalRatePerSpad;

			// "clip to 16 bit"
			if (requiredSpads > 0xFFFF) { requiredSpads = 0xFFFF; }

			// "override DSS config"
			VL53L1X_writeReg16Bit(DSS_CONFIG__MANUAL_EFFECTIVE_SPADS_SELECT, requiredSpads);
			// DSS_CONFIG__ROI_MODE_CONTROL should already be set to REQUESTED_EFFFECTIVE_SPADS

			return;
		}
	}

	// If we reached this point, it means something above would have resulted in a
	// divide by zero.
	// "We want to gracefully set a spad target, not just exit with an error"

	 // "set target to mid point"
	VL53L1X_writeReg16Bit(DSS_CONFIG__MANUAL_EFFECTIVE_SPADS_SELECT, 0x8000);
}

// get range, status, rates from results buffer
// based on VL53L1_GetRangingMeasurementData()
void VL53L1X_getRangingData(void)
{
	// VL53L1_copy_sys_and_core_results_to_range_results() begin

	uint16_t range = results.final_crosstalk_corrected_range_mm_sd0;

	// "apply correction gain"
	// gain factor of 2011 is tuning parm default (VL53L1_TUNINGPARM_LITE_RANGING_GAIN_FACTOR_DEFAULT)
	// Basically, this appears to scale the result by 2011/2048, or about 98%
	// (with the 1024 added for proper rounding).
	ranging_data.range_mm = ((uint32_t)range * 2011 + 0x0400) / 0x0800;

	// VL53L1_copy_sys_and_core_results_to_range_results() end

	// set range_status in ranging_data based on value of RESULT__RANGE_STATUS register
	// mostly based on ConvertStatusLite()
	switch (results.range_status)
	{
	case 17: // MULTCLIPFAIL
	case 2: // VCSELWATCHDOGTESTFAILURE
	case 1: // VCSELCONTINUITYTESTFAILURE
	case 3: // NOVHVVALUEFOUND
	  // from SetSimpleData()
		ranging_data.range_status = HardwareFail;
		break;

	case 13: // USERROICLIP
	 // from SetSimpleData()
		ranging_data.range_status = MinRangeFail;
		break;

	case 18: // GPHSTREAMCOUNT0READY
		ranging_data.range_status = SynchronizationInt;
		break;

	case 5: // RANGEPHASECHECK
		ranging_data.range_status = OutOfBoundsFail;
		break;

	case 4: // MSRCNOTARGET
		ranging_data.range_status = SignalFail;
		break;

	case 6: // SIGMATHRESHOLDCHECK
		ranging_data.range_status = SignalFail;
		break;

	case 7: // PHASECONSISTENCY
		ranging_data.range_status = WrapTargetFail;
		break;

	case 12: // RANGEIGNORETHRESHOLD
		ranging_data.range_status = XtalkSignalFail;
		break;

	case 8: // MINCLIP
		ranging_data.range_status = RangeValidMinRangeClipped;
		break;

	case 9: // RANGECOMPLETE
	  // from VL53L1_copy_sys_and_core_results_to_range_results()
		if (results.stream_count == 0)
		{
			ranging_data.range_status = RangeValidNoWrapCheckFail;
		}
		else
		{
			ranging_data.range_status = RangeValid;
		}
		break;

	default:
		ranging_data.range_status = None;
	}

	// from SetSimpleData()
	ranging_data.peak_signal_count_rate_MCPS =
		VL53L1X_countRateFixedToFloat(results.peak_signal_count_rate_crosstalk_corrected_mcps_sd0);
	ranging_data.ambient_count_rate_MCPS =
		VL53L1X_countRateFixedToFloat(results.ambient_count_rate_mcps_sd0);
}

// Decode sequence step timeout in MCLKs from register value
// based on VL53L1_decode_timeout()
uint32_t VL53L1X_decodeTimeout(uint16_t reg_val)
{
	return ((uint32_t)(reg_val & 0xFF) << (reg_val >> 8)) + 1;
}

// Encode sequence step timeout register value from timeout in MCLKs
// based on VL53L1_encode_timeout()
uint16_t VL53L1X_encodeTimeout(uint32_t timeout_mclks)
{
	// encoded format: "(LSByte * 2^MSByte) + 1"

	uint32_t ls_byte = 0;
	uint16_t ms_byte = 0;

	if (timeout_mclks > 0)
	{
		ls_byte = timeout_mclks - 1;

		while ((ls_byte & 0xFFFFFF00) > 0)
		{
			ls_byte >>= 1;
			ms_byte++;
		}

		return (ms_byte << 8) | (ls_byte & 0xFF);
	}
	else { return 0; }
}

// Convert sequence step timeout from macro periods to microseconds with given
// macro period in microseconds (12.12 format)
// based on VL53L1_calc_timeout_us()
uint32_t VL53L1X_timeoutMclksToMicroseconds(uint32_t timeout_mclks, uint32_t macro_period_us)
{
	return ((uint64_t)timeout_mclks * macro_period_us + 0x800) >> 12;
}

// Convert sequence step timeout from microseconds to macro periods with given
// macro period in microseconds (12.12 format)
// based on VL53L1_calc_timeout_mclks()
uint32_t VL53L1X_timeoutMicrosecondsToMclks(uint32_t timeout_us, uint32_t macro_period_us)
{
	return (((uint32_t)timeout_us << 12) + (macro_period_us >> 1)) / macro_period_us;
}

// Calculate macro period in microseconds (12.12 format) with given VCSEL period
// assumes fast_osc_frequency has been read and stored
// based on VL53L1_calc_macro_period_us()
uint32_t VL53L1X_calcMacroPeriod(uint8_t vcsel_period)
{
	// from VL53L1_calc_pll_period_us()
	// fast osc frequency in 4.12 format; PLL period in 0.24 format
	uint32_t pll_period_us = ((uint32_t)0x01 << 30) / fast_osc_frequency;

	// from VL53L1_decode_vcsel_period()
	uint8_t vcsel_period_pclks = (vcsel_period + 1) << 1;

	// VL53L1_MACRO_PERIOD_VCSEL_PERIODS = 2304
	uint32_t macro_period_us = (uint32_t)2304 * pll_period_us;
	macro_period_us >>= 6;
	macro_period_us *= vcsel_period_pclks;
	macro_period_us >>= 6;

	return macro_period_us;
}