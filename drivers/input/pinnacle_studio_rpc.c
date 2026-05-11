#define DT_DRV_COMPAT cirque_pinnacle

#include <pb_decode.h>
#include <pb_encode.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>
#include <trackpad.pb.h>
#include <zmk/event_manager.h>
#include <proto/zmk/custom.pb.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
#include <zmk/split/central.h>
#endif

#include "input_pinnacle.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum zmk_studio_rpc_handler_security {
    ZMK_STUDIO_RPC_HANDLER_SECURED,
    ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

typedef bool(custom_subsystem_handler)(const zmk_custom_CallRequest *request,
                                       pb_callback_t *encode_response);

struct zmk_rpc_custom_subsystem_meta {
    char **ui_urls;
    size_t ui_urls_count;
    enum zmk_studio_rpc_handler_security security;
};

struct zmk_rpc_custom_subsystem {
    char *identifier;
    struct zmk_rpc_custom_subsystem_meta *meta;
    custom_subsystem_handler *handler;
};

#define ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(...)                                                      \
    .ui_urls = (char *[]){__VA_ARGS__},                                                            \
    .ui_urls_count =                                                                               \
        COND_CODE_1(IS_EMPTY(__VA_ARGS__), (0), (UTIL_INC(NUM_VA_ARGS_LESS_1(__VA_ARGS__))))

#define ZMK_RPC_CUSTOM_SUBSYSTEM(_identifier, _meta, _handler)                                     \
    static bool _handler(const zmk_custom_CallRequest *req, pb_callback_t *res);                   \
    STRUCT_SECTION_ITERABLE(zmk_rpc_custom_subsystem, zmk_rpc_custom_subsystem_##_identifier) = {  \
        .identifier = #_identifier,                                                                \
        .meta = _meta,                                                                             \
        .handler = _handler,                                                                       \
    };

bool zmk_rpc_custom_subsystem_encode_response_payload(pb_ostream_t *stream,
                                                      const pb_field_t *payload_field,
                                                      const pb_msgdesc_t *custom_response_msgdesc,
                                                      void const *custom_response);

#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(_subsystem_identifier, response_type)             \
    static response_type zmk_rpc_custom_subsystem_response_buffer_##_subsystem_identifier;         \
    static uintptr_t zmk_rpc_custom_subsystem_response_buffer_seq_##_subsystem_identifier;         \
    static inline bool zmk_rpc_custom_subsystem_encode_response_##_subsystem_identifier(           \
        pb_ostream_t *stream, const pb_field_t *payload_field, void *const *arg) {                 \
        const uintptr_t seq = (uintptr_t)*arg;                                                     \
        if (seq != zmk_rpc_custom_subsystem_response_buffer_seq_##_subsystem_identifier) {         \
            return false;                                                                          \
        }                                                                                          \
        return zmk_rpc_custom_subsystem_encode_response_payload(                                   \
            stream, payload_field, response_type##_fields,                                         \
            &zmk_rpc_custom_subsystem_response_buffer_##_subsystem_identifier);                    \
    }                                                                                              \
    static inline response_type                                                                    \
        *zmk_rpc_custom_subsystem_response_buffer_allocate_##_subsystem_identifier(                \
            pb_callback_t *encode_response) {                                                      \
        zmk_rpc_custom_subsystem_response_buffer_##_subsystem_identifier =                         \
            (response_type)response_type##_init_zero;                                              \
        encode_response->funcs.encode =                                                            \
            zmk_rpc_custom_subsystem_encode_response_##_subsystem_identifier;                      \
        encode_response->arg = (void *)(++zmk_rpc_custom_subsystem_response_buffer_seq_##_subsystem_identifier); \
        return &zmk_rpc_custom_subsystem_response_buffer_##_subsystem_identifier;                  \
    }

#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(_subsystem_identifier, encode_response)  \
    zmk_rpc_custom_subsystem_response_buffer_allocate_##_subsystem_identifier(encode_response)

static struct zmk_rpc_custom_subsystem_meta trackpad_rpc_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://te9no.github.io/zmk-module-cirque-trackpad/"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(dya__trackpad, &trackpad_rpc_meta, dya_trackpad_rpc_handle_request);
ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(dya__trackpad, dya_trackpad_Response);

#if IS_ENABLED(CONFIG_INPUT_PINNACLE) && DT_HAS_COMPAT_STATUS_OKAY(cirque_pinnacle)
#define PINNACLE_DEV_REF(n) DEVICE_DT_GET(DT_DRV_INST(n)),
static const struct device *const pinnacle_devices[] = {DT_INST_FOREACH_STATUS_OKAY(PINNACLE_DEV_REF)};
#else
static const struct device *const pinnacle_devices[] = {NULL};
#endif

static const struct device *get_device_by_id(uint32_t id) {
    if (id >= ARRAY_SIZE(pinnacle_devices)) {
        return NULL;
    }
    const struct device *dev = pinnacle_devices[id];
    if (dev == NULL) {
        return NULL;
    }
    if (!device_is_ready(dev)) {
        return NULL;
    }
    return dev;
}

static dya_trackpad_TrackpadDevice list_devices_buf[8];
static size_t list_devices_count;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)

enum trackpad_split_request_type {
    TRACKPAD_SPLIT_REQUEST_LIST = 1,
    TRACKPAD_SPLIT_REQUEST_GET = 2,
    TRACKPAD_SPLIT_REQUEST_SET_SLEEP = 3,
    TRACKPAD_SPLIT_REQUEST_RESET = 4,
    TRACKPAD_SPLIT_REQUEST_SET_DEVICE = 5,
};

#define TRACKPAD_SPLIT_REQUEST_DATA_SIZE 8
#define TRACKPAD_SPLIT_REQUEST_CHUNK_DONE 0x80

struct trackpad_split_request {
    uint8_t source;
    uint8_t seq;
    uint8_t type;
    uint8_t id;
    uint8_t chunk;
    uint8_t total_len;
    uint8_t data[TRACKPAD_SPLIT_REQUEST_DATA_SIZE];
} __packed;

#define TRACKPAD_SPLIT_RESPONSE_DATA_SIZE 9
#define TRACKPAD_SPLIT_RESPONSE_CHUNK_DONE 0x80

struct trackpad_split_response {
    uint8_t source;
    uint8_t seq;
    int8_t status;
    uint8_t chunk;
    uint8_t total_len;
    uint8_t data[TRACKPAD_SPLIT_RESPONSE_DATA_SIZE];
} __packed;

ZMK_EVENT_DECLARE(trackpad_split_request);
ZMK_EVENT_DECLARE(trackpad_split_response);
ZMK_EVENT_IMPL(trackpad_split_request);
ZMK_EVENT_IMPL(trackpad_split_response);

ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL(trackpad_split_request, tq, source);
ZMK_RELAY_EVENT_HANDLE(trackpad_split_request, tq, source);
ZMK_RELAY_EVENT_PERIPHERAL_TO_CENTRAL(trackpad_split_response, ts, source);
ZMK_RELAY_EVENT_HANDLE(trackpad_split_response, ts, source);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
K_SEM_DEFINE(split_response_sem, 0, 1);
static int split_response_status;
static uint8_t split_response_buf[sizeof(dya_trackpad_TrackpadDevice)];
static uint8_t split_response_len;
static uint8_t split_response_source;
static uint8_t split_request_seq;
#endif

static uint8_t split_request_buf[sizeof(dya_trackpad_TrackpadDevice)];
static uint8_t split_request_len;
static uint8_t split_request_seq_active;
static uint8_t split_request_source;

#endif

static bool encode_list_devices(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    ARG_UNUSED(arg);
    for (size_t i = 0; i < list_devices_count; i++) {
        if (!pb_encode_tag_for_field(stream, field)) {
            return false;
        }
        if (!pb_encode_submessage(stream, dya_trackpad_TrackpadDevice_fields, &list_devices_buf[i])) {
            return false;
        }
    }
    return true;
}

static void set_error(dya_trackpad_Response *resp, const char *message) {
    resp->which_response_type = dya_trackpad_Response_error_tag;
    snprintf(resp->response_type.error.message, sizeof(resp->response_type.error.message), "%s",
             message);
}

static bool device_sleep_enabled(const struct device *dev) {
    const struct pinnacle_config *cfg = dev->config;
    uint8_t sys_cfg = 0;
    if (cfg->seq_read(dev, PINNACLE_SYS_CFG, &sys_cfg, 1) < 0) {
        return false;
    }
    return (sys_cfg & PINNACLE_SYS_CFG_EN_SLEEP) != 0;
}

static void fill_device_info(const struct device *dev, uint32_t id, dya_trackpad_TrackpadDevice *out) {
    const struct pinnacle_config *cfg = dev->config;
    *out = (dya_trackpad_TrackpadDevice)dya_trackpad_TrackpadDevice_init_zero;

    out->id = id;
    snprintf(out->name, sizeof(out->name), "%s", dev->name);
    out->rotate90 = cfg->rotate_90;
    out->xInvert = cfg->x_invert;
    out->yInvert = cfg->y_invert;
    out->sleep = device_sleep_enabled(dev);
    out->noTaps = cfg->no_taps;
    out->noSecondaryTap = cfg->no_secondary_tap;
    out->absoluteGestures = cfg->absolute_gestures;
    out->tapToClick = cfg->tap_to_click;
    out->doubleTapDrag = cfg->double_tap_drag;
    out->reverseCircularScroll = cfg->reverse_circular_scroll;
    out->sensitivity = (dya_trackpad_Sensitivity)cfg->sensitivity;
    out->xAxisZMin = cfg->x_axis_z_min;
    out->yAxisZMin = cfg->y_axis_z_min;
    out->touchZThreshold = cfg->touch_z_threshold;
    out->xMax = cfg->x_max;
    out->yMax = cfg->y_max;
    out->edgeScrollMargin = cfg->edge_scroll_margin;
    out->pointerDivisor = cfg->pointer_divisor;
    out->tapTimeoutMs = cfg->tap_timeout_ms;
    out->doubleTapMs = cfg->double_tap_ms;
    out->tapMoveThreshold = cfg->tap_move_threshold;
    out->scrollStep = cfg->scroll_step;
    out->ready = device_is_ready(dev);
    out->inertiaEnabled = cfg->inertia_enabled;
    out->inertiaDecay = cfg->inertia_decay;
    out->inertiaMinVelocity = cfg->inertia_min_velocity;
    out->inertiaMaxTicks = cfg->inertia_max_ticks;
}

static int fill_first_local_device(dya_trackpad_TrackpadDevice *out) {
    for (size_t i = 0; i < ARRAY_SIZE(pinnacle_devices); i++) {
        const struct device *dev = pinnacle_devices[i];
        if (dev == NULL || !device_is_ready(dev)) {
            continue;
        }
        fill_device_info(dev, (uint32_t)i, out);
        return 0;
    }

    return -ENOENT;
}

static int fill_local_device_by_id(uint32_t id, dya_trackpad_TrackpadDevice *out) {
    const struct device *dev = get_device_by_id(id);
    if (dev == NULL) {
        return -ENOENT;
    }

    fill_device_info(dev, id, out);
    return 0;
}

static int reset_local_device_by_id(uint32_t id) {
    const struct device *dev = get_device_by_id(id);
    if (dev == NULL) {
        return -ENOENT;
    }

    const struct pinnacle_config *cfg = dev->config;
    uint8_t sys_cfg = 0;
    int rc = cfg->seq_read(dev, PINNACLE_SYS_CFG, &sys_cfg, 1);
    if (rc != 0) {
        return rc;
    }
    rc = cfg->write(dev, PINNACLE_SYS_CFG, sys_cfg | PINNACLE_SYS_CFG_RESET);
    if (rc != 0) {
        return rc;
    }
    k_msleep(20);
    return 0;
}

#if IS_ENABLED(CONFIG_SETTINGS) && IS_ENABLED(CONFIG_INPUT_PINNACLE)

#define TRACKPAD_SETTINGS_VERSION 1
#define TRACKPAD_SETTINGS_PREFIX "dya/trackpad"

struct trackpad_persisted_config {
    uint8_t version;
    bool rotate_90;
    bool x_invert;
    bool y_invert;
    bool sleep_en;
    bool no_taps;
    bool no_secondary_tap;
    bool absolute_gestures;
    bool tap_to_click;
    bool double_tap_drag;
    bool reverse_circular_scroll;
    bool inertia_enabled;
    uint8_t sensitivity;
    uint8_t x_axis_z_min;
    uint8_t y_axis_z_min;
    uint8_t touch_z_threshold;
    uint16_t x_max;
    uint16_t y_max;
    uint16_t edge_scroll_margin;
    uint16_t pointer_divisor;
    uint16_t tap_timeout_ms;
    uint16_t double_tap_ms;
    uint16_t tap_move_threshold;
    uint16_t scroll_step;
    uint16_t inertia_decay;
    uint16_t inertia_min_velocity;
    uint16_t inertia_max_ticks;
};

static bool trackpad_settings_loading;

static int local_device_id(const struct device *dev, uint32_t *id) {
    for (size_t i = 0; i < ARRAY_SIZE(pinnacle_devices); i++) {
        if (pinnacle_devices[i] == dev) {
            *id = (uint32_t)i;
            return 0;
        }
    }

    return -ENOENT;
}

static void persisted_from_config(const struct pinnacle_config *cfg,
                                  struct trackpad_persisted_config *saved) {
    *saved = (struct trackpad_persisted_config){
        .version = TRACKPAD_SETTINGS_VERSION,
        .rotate_90 = cfg->rotate_90,
        .x_invert = cfg->x_invert,
        .y_invert = cfg->y_invert,
        .sleep_en = cfg->sleep_en,
        .no_taps = cfg->no_taps,
        .no_secondary_tap = cfg->no_secondary_tap,
        .absolute_gestures = cfg->absolute_gestures,
        .tap_to_click = cfg->tap_to_click,
        .double_tap_drag = cfg->double_tap_drag,
        .reverse_circular_scroll = cfg->reverse_circular_scroll,
        .inertia_enabled = cfg->inertia_enabled,
        .sensitivity = (uint8_t)cfg->sensitivity,
        .x_axis_z_min = cfg->x_axis_z_min,
        .y_axis_z_min = cfg->y_axis_z_min,
        .touch_z_threshold = cfg->touch_z_threshold,
        .x_max = cfg->x_max,
        .y_max = cfg->y_max,
        .edge_scroll_margin = cfg->edge_scroll_margin,
        .pointer_divisor = cfg->pointer_divisor,
        .tap_timeout_ms = cfg->tap_timeout_ms,
        .double_tap_ms = cfg->double_tap_ms,
        .tap_move_threshold = cfg->tap_move_threshold,
        .scroll_step = cfg->scroll_step,
        .inertia_decay = cfg->inertia_decay,
        .inertia_min_velocity = cfg->inertia_min_velocity,
        .inertia_max_ticks = cfg->inertia_max_ticks,
    };
}

static void device_from_persisted(uint32_t id, const struct trackpad_persisted_config *saved,
                                  dya_trackpad_TrackpadDevice *device) {
    *device = (dya_trackpad_TrackpadDevice)dya_trackpad_TrackpadDevice_init_zero;
    device->id = id;
    device->rotate90 = saved->rotate_90;
    device->xInvert = saved->x_invert;
    device->yInvert = saved->y_invert;
    device->sleep = saved->sleep_en;
    device->noTaps = saved->no_taps;
    device->noSecondaryTap = saved->no_secondary_tap;
    device->absoluteGestures = saved->absolute_gestures;
    device->tapToClick = saved->tap_to_click;
    device->doubleTapDrag = saved->double_tap_drag;
    device->reverseCircularScroll = saved->reverse_circular_scroll;
    device->inertiaEnabled = saved->inertia_enabled;
    device->sensitivity = (dya_trackpad_Sensitivity)saved->sensitivity;
    device->xAxisZMin = saved->x_axis_z_min;
    device->yAxisZMin = saved->y_axis_z_min;
    device->touchZThreshold = saved->touch_z_threshold;
    device->xMax = saved->x_max;
    device->yMax = saved->y_max;
    device->edgeScrollMargin = saved->edge_scroll_margin;
    device->pointerDivisor = saved->pointer_divisor;
    device->tapTimeoutMs = saved->tap_timeout_ms;
    device->doubleTapMs = saved->double_tap_ms;
    device->tapMoveThreshold = saved->tap_move_threshold;
    device->scrollStep = saved->scroll_step;
    device->inertiaDecay = saved->inertia_decay;
    device->inertiaMinVelocity = saved->inertia_min_velocity;
    device->inertiaMaxTicks = saved->inertia_max_ticks;
}

static int save_local_device_settings(const struct device *dev) {
    uint32_t id;
    int rc = local_device_id(dev, &id);
    if (rc != 0) {
        return rc;
    }

    char key[32];
    struct trackpad_persisted_config saved;
    const struct pinnacle_config *cfg = dev->config;

    persisted_from_config(cfg, &saved);
    snprintf(key, sizeof(key), TRACKPAD_SETTINGS_PREFIX "/%u", id);
    rc = settings_save_one(key, &saved, sizeof(saved));
    if (rc != 0) {
        LOG_ERR("Failed to save trackpad settings %s: %d", key, rc);
    }

    return rc;
}

#endif

static int set_local_sleep_by_id(uint32_t id, bool enabled) {
    const struct device *dev = get_device_by_id(id);
    if (dev == NULL) {
        return -ENOENT;
    }

#if IS_ENABLED(CONFIG_INPUT_PINNACLE)
    int rc = pinnacle_set_sleep(dev, enabled);
    if (rc != 0) {
        return rc;
    }

    struct pinnacle_config *cfg = (struct pinnacle_config *)dev->config;
    cfg->sleep_en = enabled;
#if IS_ENABLED(CONFIG_SETTINGS) && IS_ENABLED(CONFIG_INPUT_PINNACLE)
    if (!trackpad_settings_loading) {
        return save_local_device_settings(dev);
    }
#endif
    return 0;
#else
    return -ENODEV;
#endif
}

static int apply_local_device_by_id(const dya_trackpad_TrackpadDevice *device) {
    const struct device *dev = get_device_by_id(device->id);
    if (dev == NULL) {
        return -ENOENT;
    }

#if IS_ENABLED(CONFIG_INPUT_PINNACLE)
    struct pinnacle_config *cfg = (struct pinnacle_config *)dev->config;
    const bool hardware_tuning =
        cfg->sensitivity != (enum pinnacle_sensitivity)device->sensitivity ||
        cfg->x_axis_z_min != MIN(device->xAxisZMin, UINT8_MAX) ||
        cfg->y_axis_z_min != MIN(device->yAxisZMin, UINT8_MAX);

    cfg->rotate_90 = device->rotate90;
    cfg->x_invert = device->xInvert;
    cfg->y_invert = device->yInvert;
    cfg->sleep_en = device->sleep;
    cfg->no_taps = device->noTaps;
    cfg->no_secondary_tap = device->noSecondaryTap;
    cfg->absolute_gestures = device->absoluteGestures;
    cfg->tap_to_click = device->tapToClick;
    cfg->double_tap_drag = device->doubleTapDrag;
    cfg->reverse_circular_scroll = device->reverseCircularScroll;
    cfg->sensitivity = (enum pinnacle_sensitivity)MIN((uint32_t)device->sensitivity,
                                                       (uint32_t)PINNACLE_SENSITIVITY_4X);
    cfg->x_axis_z_min = MIN(device->xAxisZMin, UINT8_MAX);
    cfg->y_axis_z_min = MIN(device->yAxisZMin, UINT8_MAX);
    cfg->touch_z_threshold = MIN(device->touchZThreshold, UINT8_MAX);
    cfg->x_max = MIN(device->xMax, UINT16_MAX);
    cfg->y_max = MIN(device->yMax, UINT16_MAX);
    cfg->edge_scroll_margin = MIN(device->edgeScrollMargin, UINT16_MAX);
    cfg->pointer_divisor = MAX(1U, MIN(device->pointerDivisor, UINT16_MAX));
    cfg->tap_timeout_ms = MIN(device->tapTimeoutMs, UINT16_MAX);
    cfg->double_tap_ms = MIN(device->doubleTapMs, UINT16_MAX);
    cfg->tap_move_threshold = MIN(device->tapMoveThreshold, UINT16_MAX);
    cfg->scroll_step = MAX(1U, MIN(device->scrollStep, UINT16_MAX));
    cfg->inertia_enabled = device->inertiaEnabled;
    cfg->inertia_decay = CLAMP(device->inertiaDecay, 1U, 999U);
    cfg->inertia_min_velocity = MAX(1U, MIN(device->inertiaMinVelocity, UINT16_MAX));
    cfg->inertia_max_ticks = MAX(1U, MIN(device->inertiaMaxTicks, UINT16_MAX));

    int rc = pinnacle_apply_runtime_config(dev, hardware_tuning);
    if (rc != 0) {
        return rc;
    }

#if IS_ENABLED(CONFIG_SETTINGS) && IS_ENABLED(CONFIG_INPUT_PINNACLE)
    if (!trackpad_settings_loading) {
        rc = save_local_device_settings(dev);
        if (rc != 0) {
            return rc;
        }
    }
#endif

    return 0;
#else
    return -ENODEV;
#endif
}

#if IS_ENABLED(CONFIG_SETTINGS) && IS_ENABLED(CONFIG_INPUT_PINNACLE)

static int trackpad_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg) {
    const char *next;

    for (size_t i = 0; i < ARRAY_SIZE(pinnacle_devices); i++) {
        char id_key[8];
        snprintf(id_key, sizeof(id_key), "%u", (uint32_t)i);

        if (!settings_name_steq(name, id_key, &next) || next) {
            continue;
        }
        if (len != sizeof(struct trackpad_persisted_config)) {
            return -EINVAL;
        }

        struct trackpad_persisted_config saved;
        int rc = read_cb(cb_arg, &saved, sizeof(saved));
        if (rc <= 0) {
            return rc;
        }
        if (saved.version != TRACKPAD_SETTINGS_VERSION) {
            return -EINVAL;
        }

        dya_trackpad_TrackpadDevice device;
        device_from_persisted((uint32_t)i, &saved, &device);

        trackpad_settings_loading = true;
        rc = apply_local_device_by_id(&device);
        trackpad_settings_loading = false;
        return rc;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(dya_trackpad, TRACKPAD_SETTINGS_PREFIX, NULL, trackpad_settings_set,
                               NULL, NULL);

#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)

static void raise_split_response_chunks(uint8_t seq, int status,
                                        const dya_trackpad_TrackpadDevice *device) {
    const uint8_t *bytes = (const uint8_t *)device;
    const uint8_t total_len = (status == 0 && device != NULL) ? sizeof(*device) : 0;
    const uint8_t chunk_count =
        total_len == 0 ? 1 : DIV_ROUND_UP(total_len, TRACKPAD_SPLIT_RESPONSE_DATA_SIZE);

    for (uint8_t i = 0; i < chunk_count; i++) {
        const uint8_t offset = i * TRACKPAD_SPLIT_RESPONSE_DATA_SIZE;
        const uint8_t remaining = total_len > offset ? total_len - offset : 0;
        const uint8_t data_len = MIN(remaining, TRACKPAD_SPLIT_RESPONSE_DATA_SIZE);
        struct trackpad_split_response resp = {
            .source = ZMK_RELAY_EVENT_SOURCE_SELF,
            .seq = seq,
            .status = status < INT8_MIN ? INT8_MIN : (status > INT8_MAX ? INT8_MAX : status),
            .chunk = i,
            .total_len = total_len,
        };

        if (i == chunk_count - 1) {
            resp.chunk |= TRACKPAD_SPLIT_RESPONSE_CHUNK_DONE;
        }
        if (data_len > 0) {
            memcpy(resp.data, bytes + offset, data_len);
        }

        raise_trackpad_split_response(resp);
        k_msleep(2);
    }
}

static int handle_split_request_event(const zmk_event_t *eh) {
    struct trackpad_split_request *req = as_trackpad_split_request(eh);
    if (req == NULL || req->source == ZMK_RELAY_EVENT_SOURCE_SELF) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    dya_trackpad_TrackpadDevice device = dya_trackpad_TrackpadDevice_init_zero;
    int status = 0;

    switch (req->type) {
    case TRACKPAD_SPLIT_REQUEST_LIST:
        status = fill_first_local_device(&device);
        break;
    case TRACKPAD_SPLIT_REQUEST_GET:
        status = fill_local_device_by_id(req->id, &device);
        break;
    case TRACKPAD_SPLIT_REQUEST_SET_SLEEP:
        status = set_local_sleep_by_id(req->id, req->data[0] != 0);
        if (status == 0) {
            status = fill_local_device_by_id(req->id, &device);
        }
        break;
    case TRACKPAD_SPLIT_REQUEST_RESET:
        status = reset_local_device_by_id(req->id);
        if (status == 0) {
            status = fill_local_device_by_id(req->id, &device);
        }
        break;
    case TRACKPAD_SPLIT_REQUEST_SET_DEVICE: {
        const uint8_t chunk = req->chunk & ~TRACKPAD_SPLIT_REQUEST_CHUNK_DONE;
        if (chunk == 0) {
            split_request_source = req->source;
            split_request_seq_active = req->seq;
            split_request_len = req->total_len;
            memset(split_request_buf, 0, sizeof(split_request_buf));
        } else if (req->source != split_request_source || req->seq != split_request_seq_active) {
            return ZMK_EV_EVENT_BUBBLE;
        }

        const uint8_t offset = chunk * TRACKPAD_SPLIT_REQUEST_DATA_SIZE;
        if (req->total_len != split_request_len || req->total_len != sizeof(device) ||
            offset > sizeof(split_request_buf)) {
            status = -EOVERFLOW;
            break;
        }

        const uint8_t remaining = req->total_len > offset ? req->total_len - offset : 0;
        const uint8_t data_len = MIN(remaining, TRACKPAD_SPLIT_REQUEST_DATA_SIZE);
        if (data_len > 0) {
            memcpy(split_request_buf + offset, req->data, data_len);
        }

        if ((req->chunk & TRACKPAD_SPLIT_REQUEST_CHUNK_DONE) == 0) {
            return ZMK_EV_EVENT_HANDLED;
        }

        memcpy(&device, split_request_buf, sizeof(device));
        status = apply_local_device_by_id(&device);
        if (status == 0) {
            status = fill_local_device_by_id(device.id, &device);
        }
        break;
    }
    default:
        status = -ENOTSUP;
        break;
    }

    raise_split_response_chunks(req->seq, status, status == 0 ? &device : NULL);
    return ZMK_EV_EVENT_HANDLED;
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static int handle_split_response_event(const zmk_event_t *eh) {
    struct trackpad_split_response *resp = as_trackpad_split_response(eh);
    if (resp == NULL || resp->source == ZMK_RELAY_EVENT_SOURCE_SELF) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (resp->seq != split_request_seq) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint8_t chunk = resp->chunk & ~TRACKPAD_SPLIT_RESPONSE_CHUNK_DONE;
    if (chunk == 0) {
        split_response_source = resp->source;
    } else if (resp->source != split_response_source) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint8_t offset = chunk * TRACKPAD_SPLIT_RESPONSE_DATA_SIZE;
    if (resp->total_len > sizeof(split_response_buf) || offset > sizeof(split_response_buf)) {
        split_response_status = -EOVERFLOW;
        k_sem_give(&split_response_sem);
        return ZMK_EV_EVENT_HANDLED;
    }

    const uint8_t remaining = resp->total_len > offset ? resp->total_len - offset : 0;
    const uint8_t data_len = MIN(remaining, TRACKPAD_SPLIT_RESPONSE_DATA_SIZE);
    if (data_len > 0) {
        memcpy(split_response_buf + offset, resp->data, data_len);
    }

    split_response_status = resp->status;
    split_response_len = resp->total_len;

    if ((resp->chunk & TRACKPAD_SPLIT_RESPONSE_CHUNK_DONE) != 0) {
        k_sem_give(&split_response_sem);
    }
    return ZMK_EV_EVENT_HANDLED;
}

static int call_split_trackpad(uint8_t type, uint8_t id, bool enabled,
                               const dya_trackpad_TrackpadDevice *device,
                               dya_trackpad_TrackpadDevice *out) {
    split_request_seq++;
    if (split_request_seq == 0) {
        split_request_seq = 1;
    }

    while (k_sem_take(&split_response_sem, K_NO_WAIT) == 0) {
    }
    memset(split_response_buf, 0, sizeof(split_response_buf));
    split_response_len = 0;
    split_response_status = 0;
    split_response_source = 0;

    if (device != NULL) {
        const uint8_t *bytes = (const uint8_t *)device;
        const uint8_t total_len = sizeof(*device);
        const uint8_t chunk_count = DIV_ROUND_UP(total_len, TRACKPAD_SPLIT_REQUEST_DATA_SIZE);

        for (uint8_t i = 0; i < chunk_count; i++) {
            const uint8_t offset = i * TRACKPAD_SPLIT_REQUEST_DATA_SIZE;
            const uint8_t remaining = total_len > offset ? total_len - offset : 0;
            const uint8_t data_len = MIN(remaining, TRACKPAD_SPLIT_REQUEST_DATA_SIZE);
            struct trackpad_split_request req = {
                .source = ZMK_RELAY_EVENT_SOURCE_SELF,
                .seq = split_request_seq,
                .type = type,
                .id = id,
                .chunk = (uint8_t)(i | (i == chunk_count - 1 ? TRACKPAD_SPLIT_REQUEST_CHUNK_DONE : 0)),
                .total_len = total_len,
            };

            if (data_len > 0) {
                memcpy(req.data, bytes + offset, data_len);
            }

            int rc = raise_trackpad_split_request(req);
            if (rc != 0) {
                return rc;
            }
            k_msleep(2);
        }
    } else {
        struct trackpad_split_request req = {
            .source = ZMK_RELAY_EVENT_SOURCE_SELF,
            .seq = split_request_seq,
            .type = type,
            .id = id,
            .chunk = TRACKPAD_SPLIT_REQUEST_CHUNK_DONE,
            .total_len = 0,
            .data = {enabled ? 1 : 0},
        };

        int rc = raise_trackpad_split_request(req);
        if (rc != 0) {
            return rc;
        }
    }

    int rc = k_sem_take(&split_response_sem,
                        K_MSEC(CONFIG_INPUT_PINNACLE_STUDIO_RPC_SPLIT_TIMEOUT_MS));
    if (rc != 0) {
        return -ETIMEDOUT;
    }

    if (split_response_status != 0) {
        return split_response_status;
    }
    if (split_response_len != sizeof(*out)) {
        return -EIO;
    }

    memcpy(out, split_response_buf, sizeof(*out));
    return 0;
}
#endif

ZMK_LISTENER(trackpad_split_request_handler, handle_split_request_event);
ZMK_SUBSCRIPTION(trackpad_split_request_handler, trackpad_split_request);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_LISTENER(trackpad_split_response_handler, handle_split_response_event);
ZMK_SUBSCRIPTION(trackpad_split_response_handler, trackpad_split_response);
#endif

#endif

static int handle_list_devices(dya_trackpad_Response *resp) {
    dya_trackpad_ListDevicesResponse result = dya_trackpad_ListDevicesResponse_init_zero;
    list_devices_count = 0;

    for (size_t i = 0; i < ARRAY_SIZE(list_devices_buf); i++) {
        if (i >= ARRAY_SIZE(pinnacle_devices)) {
            break;
        }
        const struct device *dev = pinnacle_devices[i];
        if (dev == NULL || !device_is_ready(dev)) {
            continue;
        }
        fill_device_info(dev, (uint32_t)i, &list_devices_buf[list_devices_count]);
        list_devices_count++;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (list_devices_count == 0) {
        dya_trackpad_TrackpadDevice split_device = dya_trackpad_TrackpadDevice_init_zero;
        int rc = call_split_trackpad(TRACKPAD_SPLIT_REQUEST_LIST, 0, false, NULL, &split_device);
        if (rc == 0) {
            list_devices_buf[0] = split_device;
            list_devices_buf[0].id = 0;
            list_devices_count = 1;
        } else if (rc != -ENOENT) {
            return rc;
        }
    }
#endif

    result.devices.funcs.encode = encode_list_devices;
    result.devices.arg = NULL;

    resp->which_response_type = dya_trackpad_Response_list_devices_tag;
    resp->response_type.list_devices = result;
    return 0;
}

static int handle_get_device(const dya_trackpad_GetDeviceRequest *req, dya_trackpad_Response *resp) {
    dya_trackpad_GetDeviceResponse result = dya_trackpad_GetDeviceResponse_init_zero;
    int rc = fill_local_device_by_id(req->id, &result.device);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (rc == -ENOENT) {
        rc = call_split_trackpad(TRACKPAD_SPLIT_REQUEST_GET, (uint8_t)req->id, false, NULL,
                                 &result.device);
    }
#endif

    if (rc != 0) {
        set_error(resp, "Device not found");
        return rc;
    }

    result.has_device = true;
    resp->which_response_type = dya_trackpad_Response_get_device_tag;
    resp->response_type.get_device = result;
    return 0;
}

static int handle_set_sleep(const dya_trackpad_SetSleepRequest *req, dya_trackpad_Response *resp) {
    int rc = set_local_sleep_by_id(req->id, req->enabled);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (rc == -ENOENT) {
        dya_trackpad_TrackpadDevice split_device;
        rc = call_split_trackpad(TRACKPAD_SPLIT_REQUEST_SET_SLEEP, (uint8_t)req->id, req->enabled,
                                 NULL, &split_device);
    }
#endif

    if (rc != 0) {
        return rc;
    }

    resp->which_response_type = dya_trackpad_Response_set_sleep_tag;
    resp->response_type.set_sleep =
        (dya_trackpad_SetSleepResponse)dya_trackpad_SetSleepResponse_init_zero;
    return 0;
}

static int handle_reset_device(const dya_trackpad_ResetDeviceRequest *req, dya_trackpad_Response *resp) {
    int rc = reset_local_device_by_id(req->id);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (rc == -ENOENT) {
        dya_trackpad_TrackpadDevice split_device;
        rc = call_split_trackpad(TRACKPAD_SPLIT_REQUEST_RESET, (uint8_t)req->id, false, NULL,
                                 &split_device);
    }
#endif

    if (rc != 0) {
        return rc;
    }

    resp->which_response_type = dya_trackpad_Response_reset_device_tag;
    resp->response_type.reset_device =
        (dya_trackpad_ResetDeviceResponse)dya_trackpad_ResetDeviceResponse_init_zero;
    return 0;
}

static int handle_set_device(const dya_trackpad_SetDeviceRequest *req, dya_trackpad_Response *resp) {
    if (!req->has_device) {
        return -EINVAL;
    }

    dya_trackpad_GetDeviceResponse result = dya_trackpad_GetDeviceResponse_init_zero;
    int rc = apply_local_device_by_id(&req->device);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (rc == -ENOENT) {
        rc = call_split_trackpad(TRACKPAD_SPLIT_REQUEST_SET_DEVICE, (uint8_t)req->device.id, false,
                                 &req->device, &result.device);
        if (rc == 0) {
            result.has_device = true;
        }
    }
#endif

    if (rc != 0) {
        return rc;
    }

    if (!result.has_device) {
        rc = fill_local_device_by_id(req->device.id, &result.device);
        if (rc != 0) {
            return rc;
        }
    }

    result.has_device = true;
    resp->which_response_type = dya_trackpad_Response_get_device_tag;
    resp->response_type.get_device = result;
    return 0;
}

static bool dya_trackpad_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                            pb_callback_t *encode_response) {
    dya_trackpad_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(dya__trackpad, encode_response);
    dya_trackpad_Request req = dya_trackpad_Request_init_zero;

    if (raw_request->payload.size == 0) {
        set_error(resp, "Empty request payload");
        return true;
    }

    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, dya_trackpad_Request_fields, &req)) {
        char message[96];

        snprintf(message, sizeof(message), "Failed to decode request: %s",
                 PB_GET_ERROR(&req_stream));
        set_error(resp, message);
        LOG_WRN("Failed to decode trackpad request: %s", PB_GET_ERROR(&req_stream));
        return true;
    }

    int rc = 0;
    switch (req.which_request_type) {
    case dya_trackpad_Request_list_devices_tag:
        rc = handle_list_devices(resp);
        break;
    case dya_trackpad_Request_get_device_tag:
        rc = handle_get_device(&req.request_type.get_device, resp);
        break;
    case dya_trackpad_Request_set_sleep_tag:
        rc = handle_set_sleep(&req.request_type.set_sleep, resp);
        break;
    case dya_trackpad_Request_reset_device_tag:
        rc = handle_reset_device(&req.request_type.reset_device, resp);
        break;
    case dya_trackpad_Request_set_device_tag:
        rc = handle_set_device(&req.request_type.set_device, resp);
        break;
    default:
        rc = -ENOTSUP;
        break;
    }

    if (rc != 0 && resp->which_response_type != dya_trackpad_Response_error_tag) {
        char message[64];
        snprintf(message, sizeof(message), "Failed to process request: %d", rc);
        set_error(resp, message);
    }

    return true;
}
