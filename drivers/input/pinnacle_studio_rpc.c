#define DT_DRV_COMPAT cirque_pinnacle

#include <pb_decode.h>
#include <pb_encode.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>
#include <trackpad.pb.h>
#include <proto/zmk/custom.pb.h>

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

#define PINNACLE_DEV_REF(n) DEVICE_DT_GET(DT_DRV_INST(n)),
static const struct device *const pinnacle_devices[] = {DT_INST_FOREACH_STATUS_OKAY(PINNACLE_DEV_REF)};

static const struct device *get_device_by_id(uint32_t id) {
    if (id >= ARRAY_SIZE(pinnacle_devices)) {
        return NULL;
    }
    const struct device *dev = pinnacle_devices[id];
    if (!device_is_ready(dev)) {
        return NULL;
    }
    return dev;
}

static dya_trackpad_TrackpadDevice list_devices_buf[8];
static size_t list_devices_count;

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
}

static int handle_list_devices(dya_trackpad_Response *resp) {
    dya_trackpad_ListDevicesResponse result = dya_trackpad_ListDevicesResponse_init_zero;
    list_devices_count = 0;

    for (size_t i = 0; i < ARRAY_SIZE(list_devices_buf); i++) {
        if (i >= ARRAY_SIZE(pinnacle_devices)) {
            break;
        }
        fill_device_info(pinnacle_devices[i], (uint32_t)i, &list_devices_buf[i]);
        list_devices_count++;
    }
    result.devices.funcs.encode = encode_list_devices;
    result.devices.arg = NULL;

    resp->which_response_type = dya_trackpad_Response_list_devices_tag;
    resp->response_type.list_devices = result;
    return 0;
}

static int handle_get_device(const dya_trackpad_GetDeviceRequest *req, dya_trackpad_Response *resp) {
    const struct device *dev = get_device_by_id(req->id);
    if (dev == NULL) {
        set_error(resp, "Device not found");
        return -ENOENT;
    }

    dya_trackpad_GetDeviceResponse result = dya_trackpad_GetDeviceResponse_init_zero;
    fill_device_info(dev, req->id, &result.device);
    resp->which_response_type = dya_trackpad_Response_get_device_tag;
    resp->response_type.get_device = result;
    return 0;
}

static int handle_set_sleep(const dya_trackpad_SetSleepRequest *req, dya_trackpad_Response *resp) {
    const struct device *dev = get_device_by_id(req->id);
    if (dev == NULL) {
        set_error(resp, "Device not found");
        return -ENOENT;
    }

    int rc = pinnacle_set_sleep(dev, req->enabled);
    if (rc != 0) {
        return rc;
    }

    resp->which_response_type = dya_trackpad_Response_set_sleep_tag;
    resp->response_type.set_sleep =
        (dya_trackpad_SetSleepResponse)dya_trackpad_SetSleepResponse_init_zero;
    return 0;
}

static int handle_reset_device(const dya_trackpad_ResetDeviceRequest *req, dya_trackpad_Response *resp) {
    const struct device *dev = get_device_by_id(req->id);
    if (dev == NULL) {
        set_error(resp, "Device not found");
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

    resp->which_response_type = dya_trackpad_Response_reset_device_tag;
    resp->response_type.reset_device =
        (dya_trackpad_ResetDeviceResponse)dya_trackpad_ResetDeviceResponse_init_zero;
    return 0;
}

static bool dya_trackpad_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                            pb_callback_t *encode_response) {
    dya_trackpad_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(dya__trackpad, encode_response);
    dya_trackpad_Request req = dya_trackpad_Request_init_zero;

    if (raw_request->payload == NULL) {
        set_error(resp, "Empty request payload");
        return true;
    }

    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload->bytes, raw_request->payload->size);
    if (!pb_decode(&req_stream, dya_trackpad_Request_fields, &req)) {
        set_error(resp, "Failed to decode request");
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
