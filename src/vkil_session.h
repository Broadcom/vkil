/*
 * Copyright 2018 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#ifndef VKIL_SESSION_H
#define VKIL_SESSION_H

#include <stdint.h>

#define VKIL_MAX_CARD                4
#define VKIL_MAX_SESSION_PER_CARD    32
#define VKIL_MAX_SESSION             (VKIL_MAX_SESSION_PER_CARD * VKIL_MAX_CARD)

/**
 * Gets the session id of the current session
 * A valid session id is >= 0, an invalid session id is -1
 *
 * @return the session id
 */
int16_t vkil_get_session_id(void);

/**
 * Gets the card id of the current session
 * A valid card id is >= 0, an invalid card id is -1
 *
 * @return the card id
 */
int8_t vkil_get_card_id(void);

#endif
