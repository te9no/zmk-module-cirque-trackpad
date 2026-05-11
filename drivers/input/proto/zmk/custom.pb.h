#pragma once

#include <pb.h>
#include <stdint.h>

#ifndef CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES
#define CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES 192
#endif

typedef PB_BYTES_ARRAY_T(CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES)
    zmk_custom_CallRequest_payload_t;

typedef struct _zmk_custom_CallRequest {
    uint32_t subsystem_index;
    zmk_custom_CallRequest_payload_t payload;
} zmk_custom_CallRequest;
