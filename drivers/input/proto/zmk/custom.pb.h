#pragma once

#include <pb.h>
#include <stdint.h>

typedef struct _zmk_custom_CallRequest {
    uint32_t subsystem_index;
    pb_bytes_array_t *payload;
} zmk_custom_CallRequest;
