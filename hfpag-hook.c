/*
 * hfpag-hook.c
 * Copyright (c) 2024 @borine (https://github.com/borine/)
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/conf.h>
#include <alsa/pcm.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hfpag-session.h"
#include "shared/dbus-client.h"

struct bluealsa_hfpag {
	struct ba_dbus_ctx dbus_ctx;
	struct hfpag_session *session;
	bool session_started;
};

static int str2bdaddr(const char *str, bdaddr_t *ba) {

	unsigned int x[6];
	if (sscanf(str, "%x:%x:%x:%x:%x:%x",
				&x[5], &x[4], &x[3], &x[2], &x[1], &x[0]) != 6)
		return -1;

	size_t i;
	for (i = 0; i < 6; i++)
		ba->b[i] = x[i];

	return 0;
}

/**
 * Called when snd_pcm_hw_params() is invoked and only *after* hw_params of the
 * slave (BlueALSA) PCM has returned success.
 */
static int bluealsa_hfpag_hw_params(snd_pcm_hook_t *hook) {
	struct bluealsa_hfpag *hfpag = (struct bluealsa_hfpag*)snd_pcm_hook_get_private(hook);

	if (hfpag_session_begin(hfpag->session, &hfpag->dbus_ctx) == 0)
		hfpag->session_started = true;

	return 0;
}

static int bluealsa_hfpag_hw_free(snd_pcm_hook_t *hook) {
	struct bluealsa_hfpag *hfpag = (struct bluealsa_hfpag*)snd_pcm_hook_get_private(hook);

	if (hfpag->session_started) {
		hfpag_session_end(hfpag->session, &hfpag->dbus_ctx);
		hfpag->session_started = false;
	}
	return 0;
}

static int bluealsa_hfpag_close(snd_pcm_hook_t *hook) {
	struct bluealsa_hfpag *hfpag = (struct bluealsa_hfpag*)snd_pcm_hook_get_private(hook);
	bluealsa_dbus_connection_ctx_free(&hfpag->dbus_ctx);
	hfpag_session_free(hfpag->session);
	free(hfpag);
	snd_pcm_hook_set_private(hook, NULL);
	return 0;
}

int bluealsa_hfpag_hook_install(snd_pcm_t *pcm, snd_config_t *conf) {
	const char *device = "00:00:00:00:00:00";
	const char *service = "org.bluealsa";
	if (conf) {
		snd_config_iterator_t i, next;
		snd_config_for_each(i, next, conf) {
			snd_config_t *node = snd_config_iterator_entry(i);
			const char *id;
			if (snd_config_get_id(node, &id) < 0)
				continue;
			if (strcmp(id, "device") == 0) {
				if (snd_config_get_string(node, &device) < 0) {
					SNDERR("Invalid type for %s", id);
					return -EINVAL;
				}
				continue;
			}
			else if (strcmp(id, "service") == 0) {
				if (snd_config_get_string(node, &service) < 0) {
					SNDERR("Invalid type for %s", id);
					return -EINVAL;
				}
				continue;
			}
			SNDERR("Unknown field %s", id);
				return -EINVAL;
		}
	}

	struct bluealsa_hfpag *hfpag = calloc(1, sizeof(struct bluealsa_hfpag));
	if (hfpag == NULL)
		return -ENOMEM;

	int ret = 0;
	snd_pcm_hook_t *hook_hw_params = NULL;
	snd_pcm_hook_t *hook_hw_free = NULL;
	snd_pcm_hook_t *hook_close = NULL;

	bdaddr_t ba_addr;
	if (device == NULL || str2bdaddr(device, &ba_addr) != 0) {
		SNDERR("Invalid BT device address: %s", device);
		ret = EINVAL;
		goto fail;
	}

	DBusError err = DBUS_ERROR_INIT;

	if (!bluealsa_dbus_connection_ctx_init(&hfpag->dbus_ctx, service, &err)) {
		SNDERR("Couldn't initialize D-Bus context: %s", err.message);
		dbus_error_free(&err);
		return -EIO;
	}

	struct ba_pcm ba_pcm = { 0 };
	if (!bluealsa_dbus_get_pcm(&hfpag->dbus_ctx,
				&ba_addr,
				BA_PCM_TRANSPORT_MASK_SCO,
				snd_pcm_stream(pcm) == SND_PCM_STREAM_PLAYBACK ? BA_PCM_MODE_SINK : BA_PCM_MODE_SOURCE,
				&ba_pcm,
				&err)) {
		SNDERR("Couldn't get BlueALSA PCM: %s", err.message);
		ret = -ENODEV;
		goto fail;
	}

	if (!(ba_pcm.transport & BA_PCM_TRANSPORT_HFP_AG))
		goto fail;

	if (hfpag_session_init(&hfpag->session, ba_pcm.device_path, &ba_pcm.addr) < 0) {
		SNDERR("Cannot initialize HFP call session");
		goto fail;
	}

	if ((ret = snd_pcm_hook_add(&hook_hw_params, pcm, SND_PCM_HOOK_TYPE_HW_PARAMS, bluealsa_hfpag_hw_params, hfpag)) < 0)
		goto fail;

	if ((ret = snd_pcm_hook_add(&hook_hw_free, pcm, SND_PCM_HOOK_TYPE_HW_FREE, bluealsa_hfpag_hw_free, hfpag)) < 0)
		goto fail;

	if ((ret = snd_pcm_hook_add(&hook_close, pcm, SND_PCM_HOOK_TYPE_CLOSE, bluealsa_hfpag_close, hfpag)) < 0)
		goto fail;

	return 0;

fail:
	bluealsa_dbus_connection_ctx_free(&hfpag->dbus_ctx);
	dbus_error_free(&err);
	if (hfpag->session != NULL)
		hfpag_session_free(hfpag->session);
	free(hfpag);
	if (hook_hw_params)
		snd_pcm_hook_remove(hook_hw_params);
	if (hook_hw_free)
		snd_pcm_hook_remove(hook_hw_free);
	if (hook_close)
		snd_pcm_hook_remove(hook_close);
	return ret;
}
SND_DLSYM_BUILD_VERSION(bluealsa_hfpag_hook_install, SND_PCM_DLSYM_VERSION);
