#pragma once
#include "esp_err.h"
#include "esp_log.h"
#define ESP_RETURN_ON_FALSE(cond, err, tag, ...)                                                                       \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            (void)(tag);                                                                                               \
            return (err);                                                                                              \
        }                                                                                                              \
    } while (0)
