/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FT8XX public API
 */

#ifndef ZEPHYR_DRIVERS_MISC_CSRF_H_
#define ZEPHYR_DRIVERS_MISC_CSRF_H_

#include <stdint.h>

struct csrf_driver_api
{
    int (*foo)(const struct device *dev);
};

#endif
