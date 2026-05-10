/*
 * Copyright (c) 2026 The DYA Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ZMK_DRIVERS_DYA_ANALOG_INPUT_H_
#define ZMK_DRIVERS_DYA_ANALOG_INPUT_H_

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DYA_ANALOG_INPUT_MAX_AXES 2

enum dya_analog_response_curve {
    DYA_ANALOG_RESPONSE_CURVE_LINEAR = 0,
    DYA_ANALOG_RESPONSE_CURVE_SOFT = 1,
    DYA_ANALOG_RESPONSE_CURVE_AGGRESSIVE = 2,
};

struct dya_analog_input_axis_config {
    struct adc_dt_spec adc_channel;
    const char *label;
    uint16_t mv_mid;
    uint16_t mv_min_max;
    uint16_t mv_deadzone;
    bool invert;
    bool report_on_change_only;
    uint16_t scale_multiplier;
    uint16_t scale_divisor;
    uint16_t evt_type;
    uint16_t input_code;
    uint8_t output_min;
    uint8_t output_max;
    enum dya_analog_response_curve response_curve;
};

struct dya_analog_input_axis_runtime_config {
    struct adc_dt_spec adc_channel;
    char name[16];
    bool enabled;
    uint16_t mv_mid;
    uint16_t mv_min_max;
    uint16_t mv_deadzone;
    bool invert;
    bool report_on_change_only;
    uint16_t scale_multiplier;
    uint16_t scale_divisor;
    uint16_t evt_type;
    uint16_t input_code;
    uint8_t output_min;
    uint8_t output_max;
    enum dya_analog_response_curve response_curve;
};

struct dya_analog_input_data {
    const struct device *dev;
    struct k_mutex lock;
    struct adc_sequence as;
#if CONFIG_ADC_ASYNC
    struct k_poll_signal async_sig;
    struct k_poll_event async_evt;
#endif
    uint16_t as_buff[DYA_ANALOG_INPUT_MAX_AXES];
    int32_t delta[DYA_ANALOG_INPUT_MAX_AXES];
    int32_t prev[DYA_ANALOG_INPUT_MAX_AXES];
    uint16_t last_raw[DYA_ANALOG_INPUT_MAX_AXES];
    int32_t last_mv[DYA_ANALOG_INPUT_MAX_AXES];
    int32_t last_report_value[DYA_ANALOG_INPUT_MAX_AXES];
    struct k_work_delayable init_work;
    bool ready;

    uint32_t sampling_hz;
    uint32_t report_interval_ms;
    int64_t last_sample_time;
    int64_t last_report_time;
    bool enabled;
    bool active;

    uint8_t axes_len;
    struct dya_analog_input_axis_runtime_config axes[DYA_ANALOG_INPUT_MAX_AXES];

    struct k_work sampling_work;
    struct k_timer sampling_timer;
};

struct dya_analog_input_config {
    uint32_t sampling_hz;
    uint32_t report_interval_ms;
    uint8_t axes_len;
    struct dya_analog_input_axis_config axes[DYA_ANALOG_INPUT_MAX_AXES];
};

#define DYA_ANALOG_INPUT_SVALUE_TO_SAMPLING_HZ(svalue) ((uint32_t)(svalue).val1)
#define DYA_ANALOG_INPUT_SVALUE_TO_ENABLE(svalue) ((uint32_t)(svalue).val1)
#define DYA_ANALOG_INPUT_SVALUE_TO_ACTIVE(svalue) ((uint32_t)(svalue).val1)

enum dya_analog_input_attribute {
    DYA_ANALOG_INPUT_ATTR_SAMPLING_HZ,
    DYA_ANALOG_INPUT_ATTR_ENABLE,
    DYA_ANALOG_INPUT_ATTR_ACTIVE,
};

uint8_t dya_analog_input_device_count(void);
const struct device *dya_analog_input_device_get(uint8_t id);
int dya_analog_input_runtime_reset(const struct device *dev);
int dya_analog_input_runtime_set_sampling_hz(const struct device *dev, uint32_t hz);
int dya_analog_input_runtime_set_report_interval_ms(const struct device *dev, uint32_t interval_ms);
int dya_analog_input_runtime_set_axis(const struct device *dev, uint8_t axis_index,
                                      const struct dya_analog_input_axis_runtime_config *axis);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_DRIVERS_DYA_ANALOG_INPUT_H_ */
