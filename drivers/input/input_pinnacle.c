#define DT_DRV_COMPAT cirque_pinnacle

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>

#include "input_pinnacle.h"

LOG_MODULE_REGISTER(pinnacle, CONFIG_INPUT_LOG_LEVEL);

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

static int pinnacle_seq_read(const struct device *dev, const uint8_t addr, uint8_t *buf,
                             const uint8_t len) {
    const struct pinnacle_config *config = dev->config;
    return config->seq_read(dev, addr, buf, len);
}
static int pinnacle_write(const struct device *dev, const uint8_t addr, const uint8_t val) {
    const struct pinnacle_config *config = dev->config;
    return config->write(dev, addr, val);
}

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)

static int pinnacle_i2c_seq_read(const struct device *dev, const uint8_t addr, uint8_t *buf,
                                 const uint8_t len) {
    const struct pinnacle_config *config = dev->config;
    return i2c_burst_read_dt(&config->bus.i2c, PINNACLE_READ | addr, buf, len);
}

static int pinnacle_i2c_write(const struct device *dev, const uint8_t addr, const uint8_t val) {
    const struct pinnacle_config *config = dev->config;
    return i2c_reg_write_byte_dt(&config->bus.i2c, PINNACLE_WRITE | addr, val);
}

#endif // DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)

static int pinnacle_spi_seq_read(const struct device *dev, const uint8_t addr, uint8_t *buf,
                                 const uint8_t len) {
    const struct pinnacle_config *config = dev->config;
    uint8_t tx_buffer[len + 3], rx_dummy[3];
    tx_buffer[0] = PINNACLE_READ | addr;
    memset(&tx_buffer[1], PINNACLE_AUTOINC, len + 2);

    const struct spi_buf tx_buf[2] = {
        {
            .buf = tx_buffer,
            .len = len + 3,
        },
    };
    const struct spi_buf_set tx = {
        .buffers = tx_buf,
        .count = 1,
    };
    struct spi_buf rx_buf[2] = {
        {
            .buf = rx_dummy,
            .len = 3,
        },
        {
            .buf = buf,
            .len = len,
        },
    };
    const struct spi_buf_set rx = {
        .buffers = rx_buf,
        .count = 2,
    };
    int ret = spi_transceive_dt(&config->bus.spi, &tx, &rx);

    return ret;
}

static int pinnacle_spi_write(const struct device *dev, const uint8_t addr, const uint8_t val) {
    const struct pinnacle_config *config = dev->config;
    uint8_t tx_buffer[2] = {PINNACLE_WRITE | addr, val};
    uint8_t rx_buffer[2];

    const struct spi_buf tx_buf = {
        .buf = tx_buffer,
        .len = 2,
    };
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1,
    };

    const struct spi_buf rx_buf = {
        .buf = rx_buffer,
        .len = 2,
    };
    const struct spi_buf_set rx = {
        .buffers = &rx_buf,
        .count = 1,
    };

    const int ret = spi_transceive_dt(&config->bus.spi, &tx, &rx);

    if (ret < 0) {
        LOG_ERR("spi ret: %d", ret);
    }

    if (rx_buffer[1] != PINNACLE_FILLER) {
        LOG_ERR("bad ret val %d - %d", rx_buffer[0], rx_buffer[1]);
        return -EIO;
    }

    k_usleep(50);

    return ret;
}
#endif // DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)

static int set_int(const struct device *dev, const bool en) {
    const struct pinnacle_config *config = dev->config;
    int ret = gpio_pin_interrupt_configure_dt(&config->dr,
                                              en ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE);
    if (ret < 0) {
        LOG_ERR("can't set interrupt");
    }

    return ret;
}

static int pinnacle_clear_status(const struct device *dev) {
    int ret = pinnacle_write(dev, PINNACLE_STATUS1, 0);
    if (ret < 0) {
        LOG_ERR("Failed to clear STATUS1 register: %d", ret);
    }

    return ret;
}

static int pinnacle_era_read(const struct device *dev, const uint16_t addr, uint8_t *val) {
    int ret = 0;

    set_int(dev, false);

    ret = pinnacle_write(dev, PINNACLE_REG_ERA_HIGH_BYTE, (uint8_t)(addr >> 8));
    if (ret < 0) {
        LOG_ERR("Failed to write ERA high byte (%d)", ret);
        ret = -EIO;
        goto done;
    }

    ret = pinnacle_write(dev, PINNACLE_REG_ERA_LOW_BYTE, (uint8_t)(addr & 0x00FF));
    if (ret < 0) {
        LOG_ERR("Failed to write ERA low byte (%d)", ret);
        ret = -EIO;
        goto done;
    }

    ret = pinnacle_write(dev, PINNACLE_REG_ERA_CONTROL, PINNACLE_ERA_CONTROL_READ);
    if (ret < 0) {
        LOG_ERR("Failed to write ERA control (%d)", ret);
        ret = -EIO;
        goto done;
    }

    uint8_t control_val;
    for (int i = 0; i < 100; i++) {
        ret = pinnacle_seq_read(dev, PINNACLE_REG_ERA_CONTROL, &control_val, 1);
        if (ret < 0) {
            LOG_ERR("Failed to read ERA control (%d)", ret);
            ret = -EIO;
            goto done;
        }
        if (control_val == 0x00) {
            goto read_value;
        }
        k_msleep(1);
    }

    LOG_WRN("ERA read timeout");
    ret = -ETIMEDOUT;
    goto done;

read_value:
    ret = pinnacle_seq_read(dev, PINNACLE_REG_ERA_VALUE, val, 1);

    if (ret < 0) {
        LOG_ERR("Failed to read ERA value (%d)", ret);
        ret = -EIO;
        goto done;
    }

    ret = pinnacle_clear_status(dev);

done:
    set_int(dev, true);

    return ret;
}

static int pinnacle_era_write(const struct device *dev, const uint16_t addr, uint8_t val) {
    int ret = 0;

    set_int(dev, false);

    ret = pinnacle_write(dev, PINNACLE_REG_ERA_VALUE, val);
    if (ret < 0) {
        LOG_ERR("Failed to write ERA value (%d)", ret);
        ret = -EIO;
        goto done;
    }

    ret = pinnacle_write(dev, PINNACLE_REG_ERA_HIGH_BYTE, (uint8_t)(addr >> 8));
    if (ret < 0) {
        LOG_ERR("Failed to write ERA high byte (%d)", ret);
        ret = -EIO;
        goto done;
    }

    ret = pinnacle_write(dev, PINNACLE_REG_ERA_LOW_BYTE, (uint8_t)(addr & 0x00FF));
    if (ret < 0) {
        LOG_ERR("Failed to write ERA low byte (%d)", ret);
        ret = -EIO;
        goto done;
    }

    ret = pinnacle_write(dev, PINNACLE_REG_ERA_CONTROL, PINNACLE_ERA_CONTROL_WRITE);
    if (ret < 0) {
        LOG_ERR("Failed to write ERA control (%d)", ret);
        ret = -EIO;
        goto done;
    }

    uint8_t control_val;
    for (int i = 0; i < 100; i++) {
        ret = pinnacle_seq_read(dev, PINNACLE_REG_ERA_CONTROL, &control_val, 1);
        if (ret < 0) {
            LOG_ERR("Failed to read ERA control (%d)", ret);
            ret = -EIO;
            goto done;
        }
        if (control_val == 0x00) {
            goto clear_status;
        }
        k_msleep(1);
    }

    LOG_WRN("ERA write timeout");
    ret = -ETIMEDOUT;
    goto done;

clear_status:
    ret = pinnacle_clear_status(dev);

done:
    set_int(dev, true);

    return ret;
}

struct pinnacle_abs_sample {
    uint16_t x;
    uint16_t y;
    uint8_t z;
};

static void pinnacle_decode_abs_sample(const uint8_t *packet, struct pinnacle_abs_sample *sample) {
    sample->x = ((packet[2] & 0x0F) << 8) | packet[0];
    sample->y = ((packet[2] & 0xF0) << 4) | packet[1];
    sample->z = packet[3] & 0x3F;
}

static bool pinnacle_in_edge_scroll_zone(const struct device *dev, uint16_t x, uint16_t y) {
    const struct pinnacle_config *config = dev->config;
    const uint16_t margin = MIN(config->edge_scroll_margin, MIN(config->x_max, config->y_max) / 2);

    if (margin == 0) {
        return false;
    }

    return x <= margin || x >= (config->x_max - margin) || y <= margin ||
           y >= (config->y_max - margin);
}

static int32_t pinnacle_edge_scroll_position(const struct device *dev, uint16_t x, uint16_t y) {
    const struct pinnacle_config *config = dev->config;
    const uint16_t margin = MIN(config->edge_scroll_margin, MIN(config->x_max, config->y_max) / 2);
    const uint16_t right_edge = config->x_max - margin;
    const uint16_t bottom_edge = config->y_max - margin;

    if (y <= margin) {
        return x;
    }
    if (x >= right_edge) {
        return config->x_max + y;
    }
    if (y >= bottom_edge) {
        return config->x_max + config->y_max + (config->x_max - x);
    }

    return (2 * config->x_max) + config->y_max + (config->y_max - y);
}

static int32_t pinnacle_shortest_ring_delta(const struct device *dev, int32_t from, int32_t to) {
    const struct pinnacle_config *config = dev->config;
    const int32_t perimeter = 2 * ((int32_t)config->x_max + config->y_max);
    int32_t delta = to - from;

    if (delta > perimeter / 2) {
        delta -= perimeter;
    } else if (delta < -perimeter / 2) {
        delta += perimeter;
    }

    return delta;
}

static void pinnacle_release_drag(const struct device *dev) {
    struct pinnacle_data *data = dev->data;

    if (data->drag_active) {
        input_report_key(dev, INPUT_BTN_0, 0, true, K_FOREVER);
        data->drag_active = false;
    }
}

static void pinnacle_click_release_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct pinnacle_data *data = CONTAINER_OF(dwork, struct pinnacle_data, click_release_work);
    const struct device *dev = data->dev;

    if (data->click_pressed) {
        input_report_key(dev, INPUT_BTN_0, 0, true, K_FOREVER);
        data->click_pressed = false;
    }
}

static void pinnacle_emit_click(const struct device *dev) {
    struct pinnacle_data *data = dev->data;

    if (data->drag_active) {
        return;
    }

    if (!data->click_pressed) {
        data->click_pressed = true;
        input_report_key(dev, INPUT_BTN_0, 1, true, K_FOREVER);
    }
    k_work_reschedule(&data->click_release_work, K_MSEC(12));
}

static void pinnacle_release_click(const struct device *dev) {
    struct pinnacle_data *data = dev->data;

    if (data->click_pressed) {
        input_report_key(dev, INPUT_BTN_0, 0, true, K_FOREVER);
        data->click_pressed = false;
    }
}

static void pinnacle_deferred_click_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct pinnacle_data *data = CONTAINER_OF(dwork, struct pinnacle_data, click_work);
    const struct device *dev = data->dev;

    pinnacle_emit_click(dev);
    data->last_tap_ms = -1;
}

static void pinnacle_handle_tap_release(const struct device *dev, int64_t now_ms) {
    const struct pinnacle_config *config = dev->config;
    struct pinnacle_data *data = dev->data;

    if (!config->tap_to_click || data->moved_since_touch ||
        (now_ms - data->touch_start_ms) > config->tap_timeout_ms) {
        return;
    }

    if (config->double_tap_drag && data->last_tap_ms >= 0 &&
        (now_ms - data->last_tap_ms) <= config->double_tap_ms) {
        k_work_cancel_delayable(&data->click_work);
        if (data->drag_active) {
            pinnacle_release_drag(dev);
        } else {
            pinnacle_release_click(dev);
            k_work_cancel_delayable(&data->click_release_work);
            input_report_key(dev, INPUT_BTN_0, 1, true, K_FOREVER);
            data->drag_active = true;
        }
        data->last_tap_ms = -1;
        return;
    }

    if (config->double_tap_drag) {
        data->last_tap_ms = now_ms;
        k_work_schedule(&data->click_work, K_MSEC(config->double_tap_ms));
    } else if (!data->drag_active) {
        pinnacle_emit_click(dev);
        data->last_tap_ms = now_ms;
    }
}

static void pinnacle_clear_inertia(const struct device *dev) {
    struct pinnacle_data *data = dev->data;

    data->inertia_active = false;
    data->inertia_vx = 0;
    data->inertia_vy = 0;
    data->inertia_rem_x = 0;
    data->inertia_rem_y = 0;
    data->inertia_ticks = 0;
}

static void pinnacle_stop_inertia(const struct device *dev) {
    struct pinnacle_data *data = dev->data;

    pinnacle_clear_inertia(dev);
    k_work_cancel_delayable(&data->inertia_work);
}

static void pinnacle_inertia_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct pinnacle_data *data = CONTAINER_OF(dwork, struct pinnacle_data, inertia_work);
    const struct device *dev = data->dev;
    const struct pinnacle_config *config = dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (!data->inertia_active || data->touching || !config->inertia_enabled) {
        pinnacle_clear_inertia(dev);
        goto done;
    }

    const uint16_t max_ticks = config->inertia_max_ticks ? config->inertia_max_ticks : 1;
    if (data->inertia_ticks++ >= max_ticks) {
        pinnacle_clear_inertia(dev);
        goto done;
    }

    const uint16_t decay = CLAMP(config->inertia_decay, 1, 999);
    data->inertia_vx = (data->inertia_vx * decay) / 1000;
    data->inertia_vy = (data->inertia_vy * decay) / 1000;

    const uint16_t min_velocity = config->inertia_min_velocity ? config->inertia_min_velocity : 1000;
    if (ABS(data->inertia_vx) < min_velocity && ABS(data->inertia_vy) < min_velocity) {
        pinnacle_clear_inertia(dev);
        goto done;
    }

    data->inertia_rem_x += data->inertia_vx;
    data->inertia_rem_y += data->inertia_vy;
    const int32_t rel_x = data->inertia_rem_x / 1000;
    const int32_t rel_y = data->inertia_rem_y / 1000;
    if (rel_x == 0 && rel_y == 0) {
        k_work_schedule(&data->inertia_work, K_MSEC(16));
        goto done;
    }
    data->inertia_rem_x -= rel_x * 1000;
    data->inertia_rem_y -= rel_y * 1000;

    input_report_rel(dev, INPUT_REL_X, rel_x, false, K_FOREVER);
    input_report_rel(dev, INPUT_REL_Y, rel_y, true, K_FOREVER);
    k_work_schedule(&data->inertia_work, K_MSEC(16));

done:
    k_mutex_unlock(&data->lock);
}

static void pinnacle_start_inertia(const struct device *dev) {
    const struct pinnacle_config *config = dev->config;
    struct pinnacle_data *data = dev->data;

    if (!config->inertia_enabled || data->drag_active || data->scroll_active || !data->moved_since_touch ||
        (data->last_dx == 0 && data->last_dy == 0)) {
        pinnacle_stop_inertia(dev);
        return;
    }

    data->inertia_vx = data->last_dx;
    data->inertia_vy = data->last_dy;
    data->inertia_rem_x = 0;
    data->inertia_rem_y = 0;
    data->inertia_ticks = 0;
    data->inertia_active = true;
    k_work_schedule(&data->inertia_work, K_MSEC(16));
}

static void pinnacle_report_abs_gestures(const struct device *dev) {
    const struct pinnacle_config *config = dev->config;
    struct pinnacle_data *data = dev->data;
    struct pinnacle_abs_sample sample;
    uint8_t packet[4];
    int ret = pinnacle_seq_read(dev, PINNACLE_STATUS1, packet, 1);

    if (ret < 0) {
        LOG_ERR("read status: %d", ret);
        return;
    }

    if (packet[0] == 0xFF || !(packet[0] & PINNACLE_STATUS1_SW_DR)) {
        return;
    }

    ret = pinnacle_seq_read(dev, PINNACLE_2_2_ABS_PACKET0, packet, 4);
    if (ret < 0) {
        LOG_ERR("read absolute packet: %d", ret);
        return;
    }

    pinnacle_decode_abs_sample(packet, &sample);
    ret = pinnacle_clear_status(dev);
    if (ret < 0) {
        return;
    }
    data->in_int = false;

    const int64_t now_ms = k_uptime_get();
    const bool touching = sample.z >= config->touch_z_threshold;

    if (!touching) {
        if (data->touching) {
            pinnacle_handle_tap_release(dev, now_ms);
            pinnacle_start_inertia(dev);
        }
        data->touching = false;
        data->scroll_active = false;
        data->scroll_accum = 0;
        data->last_dx = 0;
        data->last_dy = 0;
        return;
    }

    if (!data->touching) {
        pinnacle_stop_inertia(dev);
        data->touching = true;
        data->moved_since_touch = false;
        data->scroll_active = pinnacle_in_edge_scroll_zone(dev, sample.x, sample.y);
        data->scroll_pos = pinnacle_edge_scroll_position(dev, sample.x, sample.y);
        data->last_x = sample.x;
        data->last_y = sample.y;
        data->touch_start_x = sample.x;
        data->touch_start_y = sample.y;
        data->touch_start_ms = now_ms;
        return;
    }

    const int32_t dx = (int32_t)sample.x - data->last_x;
    const int32_t dy = (int32_t)sample.y - data->last_y;
    const int32_t touch_dx = (int32_t)sample.x - data->touch_start_x;
    const int32_t touch_dy = (int32_t)sample.y - data->touch_start_y;

    if (ABS(touch_dx) > config->tap_move_threshold || ABS(touch_dy) > config->tap_move_threshold) {
        data->moved_since_touch = true;
    }

    if (!data->scroll_active && pinnacle_in_edge_scroll_zone(dev, sample.x, sample.y)) {
        data->scroll_active = true;
        data->scroll_pos = pinnacle_edge_scroll_position(dev, sample.x, sample.y);
        data->last_x = sample.x;
        data->last_y = sample.y;
        return;
    }

    if (data->scroll_active) {
        const int32_t pos = pinnacle_edge_scroll_position(dev, sample.x, sample.y);
        data->scroll_accum += pinnacle_shortest_ring_delta(dev, data->scroll_pos, pos);
        data->scroll_pos = pos;

        const uint16_t scroll_step = config->scroll_step ? config->scroll_step : 1;
        int32_t steps = data->scroll_accum / scroll_step;
        if (steps != 0) {
            data->scroll_accum -= steps * scroll_step;
            if (!config->reverse_circular_scroll) {
                steps = -steps;
            }
            input_report_rel(dev, INPUT_REL_WHEEL, steps, true, K_FOREVER);
        }
    } else {
        const uint16_t pointer_divisor = config->pointer_divisor ? config->pointer_divisor : 1;
        const int32_t scaled_x = (dx * 1000) / pointer_divisor;
        const int32_t scaled_y = (dy * 1000) / pointer_divisor;
        const int32_t rel_x = scaled_x / 1000;
        const int32_t rel_y = scaled_y / 1000;

        input_report_rel(dev, INPUT_REL_X, rel_x, false, K_FOREVER);
        input_report_rel(dev, INPUT_REL_Y, rel_y, true, K_FOREVER);
        data->last_dx = scaled_x;
        data->last_dy = scaled_y;
    }

    data->last_x = sample.x;
    data->last_y = sample.y;
}

static void pinnacle_report_data(const struct device *dev) {
    const struct pinnacle_config *config = dev->config;
    uint8_t packet[3];
    int ret;

    if (config->absolute_gestures) {
        pinnacle_report_abs_gestures(dev);
        return;
    }

    ret = pinnacle_seq_read(dev, PINNACLE_STATUS1, packet, 1);
    if (ret < 0) {
        LOG_ERR("read status: %d", ret);
        return;
    }

    LOG_HEXDUMP_DBG(packet, 1, "Pinnacle Status1");

    // Ignore 0xFF packets that indicate communcation failure, or if SW_DR isn't asserted
    if (packet[0] == 0xFF || !(packet[0] & PINNACLE_STATUS1_SW_DR)) {
        return;
    }
    ret = pinnacle_seq_read(dev, PINNACLE_2_2_PACKET0, packet, 3);
    if (ret < 0) {
        LOG_ERR("read packet: %d", ret);
        return;
    }

    LOG_HEXDUMP_DBG(packet, 3, "Pinnacle Packets");

    struct pinnacle_data *data = dev->data;
    uint8_t btn = packet[0] &
                  (PINNACLE_PACKET0_BTN_PRIM | PINNACLE_PACKET0_BTN_SEC | PINNACLE_PACKET0_BTN_AUX);

    int8_t dx = (int8_t)packet[1];
    int8_t dy = (int8_t)packet[2];

    if (packet[0] & PINNACLE_PACKET0_X_SIGN) {
        WRITE_BIT(dx, 7, 1);
    }
    if (packet[0] & PINNACLE_PACKET0_Y_SIGN) {
        WRITE_BIT(dy, 7, 1);
    }

    if (data->in_int) {
        LOG_DBG("Clearing status bit");
        ret = pinnacle_clear_status(dev);
        if (ret < 0) {
            return;
        }
        data->in_int = false;
    }

    if (!config->no_taps && (btn || data->btn_cache)) {
        for (int i = 0; i < 3; i++) {
            uint8_t btn_val = btn & BIT(i);
            if (btn_val != (data->btn_cache & BIT(i))) {
                input_report_key(dev, INPUT_BTN_0 + i, btn_val ? 1 : 0, false, K_FOREVER);
            }
        }
    }

    data->btn_cache = btn;

    input_report_rel(dev, INPUT_REL_X, dx, false, K_FOREVER);
    input_report_rel(dev, INPUT_REL_Y, dy, true, K_FOREVER);

    return;
}

static void pinnacle_work_cb(struct k_work *work) {
    struct pinnacle_data *data = CONTAINER_OF(work, struct pinnacle_data, work);

    k_mutex_lock(&data->lock, K_FOREVER);
    pinnacle_report_data(data->dev);
    k_mutex_unlock(&data->lock);
}

static void pinnacle_gpio_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct pinnacle_data *data = CONTAINER_OF(cb, struct pinnacle_data, gpio_cb);

    LOG_DBG("HW DR asserted");
    data->in_int = true;
    k_work_submit(&data->work);
}

static int pinnacle_adc_sensitivity_reg_value(enum pinnacle_sensitivity sensitivity) {
    switch (sensitivity) {
    case PINNACLE_SENSITIVITY_1X:
        return PINNACLE_TRACKING_ADC_CONFIG_1X;
    case PINNACLE_SENSITIVITY_2X:
        return PINNACLE_TRACKING_ADC_CONFIG_2X;
    case PINNACLE_SENSITIVITY_3X:
        return PINNACLE_TRACKING_ADC_CONFIG_3X;
    case PINNACLE_SENSITIVITY_4X:
        return PINNACLE_TRACKING_ADC_CONFIG_4X;
    default:
        return PINNACLE_TRACKING_ADC_CONFIG_1X;
    }
}

static int pinnacle_tune_edge_sensitivity(const struct device *dev) {
    const struct pinnacle_config *config = dev->config;
    int ret;

    uint8_t x_val;
    ret = pinnacle_era_read(dev, PINNACLE_ERA_REG_X_AXIS_WIDE_Z_MIN, &x_val);
    if (ret < 0) {
        LOG_WRN("Failed to read X val");
        return ret;
    }

    LOG_WRN("X val: %d", x_val);

    uint8_t y_val;
    ret = pinnacle_era_read(dev, PINNACLE_ERA_REG_Y_AXIS_WIDE_Z_MIN, &y_val);
    if (ret < 0) {
        LOG_WRN("Failed to read Y val");
        return ret;
    }

    LOG_WRN("Y val: %d", y_val);

    ret = pinnacle_era_write(dev, PINNACLE_ERA_REG_X_AXIS_WIDE_Z_MIN, config->x_axis_z_min);
    if (ret < 0) {
        LOG_ERR("Failed to set X-Axis Min-Z %d", ret);
        return ret;
    }
    ret = pinnacle_era_write(dev, PINNACLE_ERA_REG_Y_AXIS_WIDE_Z_MIN, config->y_axis_z_min);
    if (ret < 0) {
        LOG_ERR("Failed to set Y-Axis Min-Z %d", ret);
        return ret;
    }
    return 0;
}

static int pinnacle_set_adc_tracking_sensitivity(const struct device *dev) {
    const struct pinnacle_config *config = dev->config;

    uint8_t val;
    int ret = pinnacle_era_read(dev, PINNACLE_ERA_REG_TRACKING_ADC_CONFIG, &val);
    if (ret < 0) {
        LOG_ERR("Failed to get ADC sensitivity %d", ret);
    }

    val &= 0x3F;
    val |= pinnacle_adc_sensitivity_reg_value(config->sensitivity);

    ret = pinnacle_era_write(dev, PINNACLE_ERA_REG_TRACKING_ADC_CONFIG, val);
    if (ret < 0) {
        LOG_ERR("Failed to set ADC sensitivity %d", ret);
    }
    ret = pinnacle_era_read(dev, PINNACLE_ERA_REG_TRACKING_ADC_CONFIG, &val);
    if (ret < 0) {
        LOG_ERR("Failed to get ADC sensitivity %d", ret);
    }

    return ret;
}

static int pinnacle_force_recalibrate(const struct device *dev) {
    uint8_t val;
    int ret = pinnacle_seq_read(dev, PINNACLE_CAL_CFG, &val, 1);
    if (ret < 0) {
        LOG_ERR("Failed to get cal config %d", ret);
    }

    val |= 0x01;
    ret = pinnacle_write(dev, PINNACLE_CAL_CFG, val);
    if (ret < 0) {
        LOG_ERR("Failed to force calibration %d", ret);
        return ret;
    }

    for (int i = 0; i < 100; i++) {
        ret = pinnacle_seq_read(dev, PINNACLE_CAL_CFG, &val, 1);
        if (ret < 0) {
            LOG_ERR("Failed to poll calibration %d", ret);
            return ret;
        }
        if ((val & 0x01) == 0) {
            return 0;
        }
        k_msleep(1);
    }

    LOG_WRN("Calibration timeout");
    return -ETIMEDOUT;
}

static int pinnacle_set_sleep_locked(const struct device *dev, bool enabled) {
    uint8_t sys_cfg;
    int ret = pinnacle_seq_read(dev, PINNACLE_SYS_CFG, &sys_cfg, 1);
    if (ret < 0) {
        LOG_ERR("can't read sys config %d", ret);
        return ret;
    }

    if (((sys_cfg & PINNACLE_SYS_CFG_EN_SLEEP) != 0) == enabled) {
        return 0;
    }

    LOG_DBG("Setting sleep: %s", (enabled ? "on" : "off"));
    WRITE_BIT(sys_cfg, PINNACLE_SYS_CFG_EN_SLEEP_BIT, enabled ? 1 : 0);

    ret = pinnacle_write(dev, PINNACLE_SYS_CFG, sys_cfg);
    if (ret < 0) {
        LOG_ERR("can't write sleep config %d", ret);
        return ret;
    }

    return ret;
}

int pinnacle_set_sleep(const struct device *dev, bool enabled) {
    struct pinnacle_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    ret = pinnacle_set_sleep_locked(dev, enabled);
    k_mutex_unlock(&data->lock);

    return ret;
}

static int pinnacle_apply_feed_config(const struct device *dev) {
    const struct pinnacle_config *config = dev->config;
    int ret;

    uint8_t feed_cfg2 = config->absolute_gestures ? PINNACLE_FEED_CFG2_DIS_TAP
                                                  : (PINNACLE_FEED_CFG2_EN_IM |
                                                     PINNACLE_FEED_CFG2_EN_BTN_SCRL);
    if (config->no_taps) {
        feed_cfg2 |= PINNACLE_FEED_CFG2_DIS_TAP;
    }
    if (config->no_secondary_tap) {
        feed_cfg2 |= PINNACLE_FEED_CFG2_DIS_SEC;
    }
    if (config->rotate_90) {
        feed_cfg2 |= PINNACLE_FEED_CFG2_ROTATE_90;
    }

    ret = pinnacle_write(dev, PINNACLE_FEED_CFG2, feed_cfg2);
    if (ret < 0) {
        LOG_ERR("can't write feed cfg2 %d", ret);
        return ret;
    }

    uint8_t feed_cfg1 = PINNACLE_FEED_CFG1_EN_FEED;
    if (config->absolute_gestures) {
        feed_cfg1 |= PINNACLE_FEED_CFG1_ABS_MODE;
    }
    if (config->x_invert) {
        feed_cfg1 |= PINNACLE_FEED_CFG1_INV_X;
    }
    if (config->y_invert) {
        feed_cfg1 |= PINNACLE_FEED_CFG1_INV_Y;
    }

    ret = pinnacle_write(dev, PINNACLE_FEED_CFG1, feed_cfg1);
    if (ret < 0) {
        LOG_ERR("can't write feed cfg1 %d", ret);
    }

    return ret;
}

int pinnacle_apply_runtime_config(const struct device *dev, bool hardware_tuning) {
    const struct pinnacle_config *config = dev->config;
    struct pinnacle_data *data = dev->data;
    int ret;

    k_mutex_lock(&data->lock, K_FOREVER);
    pinnacle_release_drag(dev);
    pinnacle_release_click(dev);
    data->touching = false;
    data->moved_since_touch = false;
    data->scroll_active = false;
    data->drag_active = false;
    data->inertia_active = false;
    data->click_pressed = false;
    data->scroll_accum = 0;
    data->touch_start_x = 0;
    data->touch_start_y = 0;
    data->last_dx = 0;
    data->last_dy = 0;
    data->inertia_vx = 0;
    data->inertia_vy = 0;
    data->inertia_rem_x = 0;
    data->inertia_rem_y = 0;
    data->inertia_ticks = 0;
    data->last_tap_ms = -1;
    k_work_cancel(&data->work);
    k_work_cancel_delayable(&data->click_work);
    k_work_cancel_delayable(&data->click_release_work);
    k_work_cancel_delayable(&data->inertia_work);

    if (hardware_tuning) {
        ret = pinnacle_set_adc_tracking_sensitivity(dev);
        if (ret < 0) {
            goto done;
        }

        ret = pinnacle_tune_edge_sensitivity(dev);
        if (ret < 0) {
            goto done;
        }
    }

    ret = pinnacle_write(dev, PINNACLE_SLEEP_INTERVAL, 255);
    if (ret < 0) {
        goto done;
    }

    ret = pinnacle_apply_feed_config(dev);
    if (ret < 0) {
        goto done;
    }

    ret = pinnacle_set_sleep_locked(dev, config->sleep_en);

done:
    k_mutex_unlock(&data->lock);
    return ret;
}

static int pinnacle_init(const struct device *dev) {
    struct pinnacle_data *data = dev->data;
    const struct pinnacle_config *config = dev->config;
    int ret;

    data->dev = dev;
    k_mutex_init(&data->lock);

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
    if (config->seq_read == pinnacle_i2c_seq_read && !device_is_ready(config->bus.i2c.bus)) {
        LOG_ERR("I2C bus is not ready");
        return -ENODEV;
    }
#endif
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
    if (config->seq_read == pinnacle_spi_seq_read && !device_is_ready(config->bus.spi.bus)) {
        LOG_ERR("SPI bus is not ready");
        return -ENODEV;
    }
#endif

    uint8_t fw_id[2];
    ret = pinnacle_seq_read(dev, PINNACLE_FW_ID, fw_id, 2);
    if (ret < 0) {
        LOG_ERR("Failed to get the FW ID %d", ret);
        return ret;
    }

    LOG_DBG("Found device with FW ID: 0x%02x, Version: 0x%02x", fw_id[0], fw_id[1]);

    data->in_int = false;
    data->touching = false;
    data->moved_since_touch = false;
    data->scroll_active = false;
    data->drag_active = false;
    data->inertia_active = false;
    data->click_pressed = false;
    data->scroll_accum = 0;
    data->touch_start_x = 0;
    data->touch_start_y = 0;
    data->last_dx = 0;
    data->last_dy = 0;
    data->inertia_vx = 0;
    data->inertia_vy = 0;
    data->inertia_rem_x = 0;
    data->inertia_rem_y = 0;
    data->inertia_ticks = 0;
    data->last_tap_ms = -1;
    k_msleep(10);
    ret = pinnacle_write(dev, PINNACLE_STATUS1, 0); // Clear CC
    if (ret < 0) {
        LOG_ERR("can't write %d", ret);
        return ret;
    }
    k_usleep(50);
    ret = pinnacle_write(dev, PINNACLE_SYS_CFG, PINNACLE_SYS_CFG_RESET);
    if (ret < 0) {
        LOG_ERR("can't reset %d", ret);
        return ret;
    }
    k_msleep(20);
    ret = pinnacle_write(dev, PINNACLE_Z_IDLE, 0x05); // No Z-Idle packets
    if (ret < 0) {
        LOG_ERR("can't write %d", ret);
        return ret;
    }

    ret = pinnacle_set_adc_tracking_sensitivity(dev);
    if (ret < 0) {
        LOG_ERR("Failed to set ADC sensitivity %d", ret);
        return ret;
    }

    ret = pinnacle_tune_edge_sensitivity(dev);
    if (ret < 0) {
        LOG_ERR("Failed to tune edge sensitivity %d", ret);
        return ret;
    }
    ret = pinnacle_force_recalibrate(dev);
    if (ret < 0) {
        LOG_ERR("Failed to force recalibration %d", ret);
        return ret;
    }

    uint8_t packet[1];
    ret = pinnacle_seq_read(dev, PINNACLE_SLEEP_INTERVAL, packet, 1);

    if (ret >= 0) {
        LOG_DBG("Default sleep interval %d", packet[0]);
    }

    ret = pinnacle_write(dev, PINNACLE_SLEEP_INTERVAL, 255);
    if (ret < 0) {
        LOG_ERR("Failed to update sleep interval %d", ret);
        return ret;
    }

    ret = pinnacle_apply_feed_config(dev);
    if (ret < 0) {
        return ret;
    }

    ret = pinnacle_set_sleep_locked(dev, config->sleep_en);
    if (ret < 0) {
        return ret;
    }

    pinnacle_clear_status(dev);

    if (!gpio_is_ready_dt(&config->dr)) {
        LOG_ERR("DR GPIO is not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->dr, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure DR GPIO: %d", ret);
        return ret;
    }
    gpio_init_callback(&data->gpio_cb, pinnacle_gpio_cb, BIT(config->dr.pin));
    ret = gpio_add_callback(config->dr.port, &data->gpio_cb);
    if (ret < 0) {
        LOG_ERR("Failed to set DR callback: %d", ret);
        return -EIO;
    }

    k_work_init(&data->work, pinnacle_work_cb);
    k_work_init_delayable(&data->click_work, pinnacle_deferred_click_cb);
    k_work_init_delayable(&data->click_release_work, pinnacle_click_release_cb);
    k_work_init_delayable(&data->inertia_work, pinnacle_inertia_work_cb);

    ret = set_int(dev, true);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

#if IS_ENABLED(CONFIG_PM_DEVICE)

static int pinnacle_pm_action(const struct device *dev, enum pm_device_action action) {
    struct pinnacle_data *data = dev->data;

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        pinnacle_stop_inertia(dev);
        k_work_cancel(&data->work);
        k_work_cancel_delayable(&data->click_work);
        k_work_cancel_delayable(&data->click_release_work);
        pinnacle_release_click(dev);
        return set_int(dev, false);
    case PM_DEVICE_ACTION_RESUME:
        return set_int(dev, true);
    default:
        return -ENOTSUP;
    }
}

#endif // IS_ENABLED(CONFIG_PM_DEVICE)

#define PINNACLE_INST(n)                                                                           \
    static struct pinnacle_data pinnacle_data_##n;                                                 \
    static struct pinnacle_config pinnacle_config_##n = {                                          \
        COND_CODE_1(DT_INST_ON_BUS(n, i2c),                                                        \
                    (.bus = {.i2c = I2C_DT_SPEC_INST_GET(n)}, .seq_read = pinnacle_i2c_seq_read,   \
                     .write = pinnacle_i2c_write),                                                 \
                    (.bus = {.spi = SPI_DT_SPEC_INST_GET(n,                                        \
                                                         SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |    \
                                                             SPI_TRANSFER_MSB | SPI_MODE_CPHA,     \
                                                         0)},                                      \
                     .seq_read = pinnacle_spi_seq_read, .write = pinnacle_spi_write)),             \
        .rotate_90 = DT_INST_PROP(n, rotate_90),                                                   \
        .x_invert = DT_INST_PROP(n, x_invert),                                                     \
        .y_invert = DT_INST_PROP(n, y_invert),                                                     \
        .sleep_en = DT_INST_PROP(n, sleep),                                                        \
        .no_taps = DT_INST_PROP(n, no_taps),                                                       \
        .no_secondary_tap = DT_INST_PROP(n, no_secondary_tap),                                     \
        .absolute_gestures = DT_INST_PROP(n, absolute_gestures),                                    \
        .tap_to_click = DT_INST_PROP(n, tap_to_click),                                             \
        .double_tap_drag = DT_INST_PROP(n, double_tap_drag),                                       \
        .reverse_circular_scroll = DT_INST_PROP(n, reverse_circular_scroll),                       \
        .x_axis_z_min = DT_INST_PROP_OR(n, x_axis_z_min, 5),                                       \
        .y_axis_z_min = DT_INST_PROP_OR(n, y_axis_z_min, 4),                                       \
        .touch_z_threshold = DT_INST_PROP_OR(n, touch_z_threshold, 8),                             \
        .x_max = DT_INST_PROP_OR(n, x_max, 2047),                                                  \
        .y_max = DT_INST_PROP_OR(n, y_max, 2047),                                                  \
        .edge_scroll_margin = DT_INST_PROP_OR(n, edge_scroll_margin, 220),                         \
        .pointer_divisor = DT_INST_PROP_OR(n, pointer_divisor, 4),                                  \
        .tap_timeout_ms = DT_INST_PROP_OR(n, tap_timeout_ms, 250),                                  \
        .double_tap_ms = DT_INST_PROP_OR(n, double_tap_ms, 240),                                   \
        .tap_move_threshold = DT_INST_PROP_OR(n, tap_move_threshold, 140),                          \
        .scroll_step = DT_INST_PROP_OR(n, scroll_step, 160),                                       \
        .inertia_enabled = DT_INST_PROP(n, inertia_enabled),                                       \
        .inertia_decay = DT_INST_PROP_OR(n, inertia_decay, 850),                                   \
        .inertia_min_velocity = DT_INST_PROP_OR(n, inertia_min_velocity, 1000),                     \
        .inertia_max_ticks = DT_INST_PROP_OR(n, inertia_max_ticks, 24),                             \
        .sensitivity = DT_INST_ENUM_IDX_OR(n, sensitivity, PINNACLE_SENSITIVITY_1X),               \
        .dr = GPIO_DT_SPEC_GET_OR(DT_DRV_INST(n), dr_gpios, {}),                                   \
    };                                                                                             \
    PM_DEVICE_DT_INST_DEFINE(n, pinnacle_pm_action);                                               \
    DEVICE_DT_INST_DEFINE(n, pinnacle_init, PM_DEVICE_DT_INST_GET(n), &pinnacle_data_##n,          \
                          &pinnacle_config_##n, POST_KERNEL, CONFIG_INPUT_PINNACLE_INIT_PRIORITY,  \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(PINNACLE_INST)
