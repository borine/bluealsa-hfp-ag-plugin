/*
 * hfpag-session.h
 * Copyright (c) 2023 @borine (https://github.com/borine/)
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#pragma once
#ifndef HFPAG_SESSION_H_
#define HFPAG_SESSION_H_

#include <bluetooth/bluetooth.h>
#include <dbus/dbus.h>
#include <limits.h>

#include "shared/dbus-client.h"

struct hfpag_session {
	char rfcomm_path[128];
	char lock_file[PATH_MAX + 1];
	int lock_fd;
};

int hfpag_session_init(struct hfpag_session **phfpag, const char *device_path, const bdaddr_t *addr);
int hfpag_session_begin(struct hfpag_session *hfpag, struct ba_dbus_ctx *dbus_ctx);
int hfpag_session_end(struct hfpag_session *hfpag, struct ba_dbus_ctx *dbus_ctx);
void hfpag_session_free(struct hfpag_session *hfpag);

#endif
