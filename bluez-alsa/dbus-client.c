/*
 * BlueALSA - dbus-client.c
 * Copyright (c) 2016-2022 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "dbus-client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int path2ba(const char *path, bdaddr_t *ba) {

	unsigned int x[6];
	if ((path = strstr(path, "/dev_")) == NULL ||
			sscanf(&path[5], "%x_%x_%x_%x_%x_%x",
				&x[5], &x[4], &x[3], &x[2], &x[1], &x[0]) != 6)
		return -1;

	size_t i;
	for (i = 0; i < 6; i++)
		ba->b[i] = x[i];

	return 0;
}

dbus_bool_t bluealsa_dbus_connection_ctx_init(
		struct ba_dbus_ctx *ctx,
		const char *ba_service_name,
		DBusError *error) {

	/* Zero-out context structure, so it will be
	 * safe to call *_ctx_free() upon error. */
	memset(ctx, 0, sizeof(*ctx));

	if ((ctx->conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, error)) == NULL)
		return FALSE;

	/* do not terminate in case of D-Bus connection being lost */
	dbus_connection_set_exit_on_disconnect(ctx->conn, FALSE);

	strncpy(ctx->ba_service, ba_service_name, sizeof(ctx->ba_service) - 1);

	return TRUE;
}

void bluealsa_dbus_connection_ctx_free(
		struct ba_dbus_ctx *ctx) {
	if (ctx->conn != NULL) {
		dbus_connection_close(ctx->conn);
		dbus_connection_unref(ctx->conn);
		ctx->conn = NULL;
	}
	if (ctx->watches != NULL) {
		free(ctx->watches);
		ctx->watches = NULL;
	}
	if (ctx->matches != NULL) {
		size_t i;
		for (i = 0; i < ctx->matches_len; i++)
			free(ctx->matches[i]);
		free(ctx->matches);
		ctx->matches = NULL;
	}
}

dbus_bool_t bluealsa_dbus_get_pcms(
		struct ba_dbus_ctx *ctx,
		struct ba_pcm **pcms,
		size_t *length,
		DBusError *error) {

	DBusMessage *msg;
	if ((msg = dbus_message_new_method_call(ctx->ba_service, "/org/bluealsa",
					DBUS_INTERFACE_OBJECT_MANAGER, "GetManagedObjects")) == NULL) {
		dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
		return FALSE;
	}

	dbus_bool_t rv = TRUE;
	struct ba_pcm *_pcms = NULL;
	size_t _length = 0;

	DBusMessage *rep;
	if ((rep = dbus_connection_send_with_reply_and_block(ctx->conn,
					msg, DBUS_TIMEOUT_USE_DEFAULT, error)) == NULL)
		goto fail;

	DBusMessageIter iter;
	if (!dbus_message_iter_init(rep, &iter)) {
		dbus_set_error(error, DBUS_ERROR_INVALID_SIGNATURE, "Empty response message");
		goto fail;
	}

	DBusMessageIter iter_objects;
	for (dbus_message_iter_recurse(&iter, &iter_objects);
			dbus_message_iter_get_arg_type(&iter_objects) != DBUS_TYPE_INVALID;
			dbus_message_iter_next(&iter_objects)) {

		if (dbus_message_iter_get_arg_type(&iter_objects) != DBUS_TYPE_DICT_ENTRY) {
			char *signature = dbus_message_iter_get_signature(&iter);
			dbus_set_error(error, DBUS_ERROR_INVALID_SIGNATURE,
					"Incorrect signature: %s != a{oa{sa{sv}}}", signature);
			dbus_free(signature);
			goto fail;
		}

		DBusMessageIter iter_object_entry;
		dbus_message_iter_recurse(&iter_objects, &iter_object_entry);

		struct ba_pcm pcm;
		DBusError err = DBUS_ERROR_INIT;
		if (!bluealsa_dbus_message_iter_get_pcm(&iter_object_entry, &err, &pcm)) {
			dbus_set_error(error, err.name, "Get PCM: %s", err.message);
			dbus_error_free(&err);
			goto fail;
		}

		if (pcm.transport == BA_PCM_TRANSPORT_NONE)
			continue;

		struct ba_pcm *tmp = _pcms;
		if ((tmp = realloc(tmp, (_length + 1) * sizeof(*tmp))) == NULL) {
			dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
			goto fail;
		}

		_pcms = tmp;

		memcpy(&_pcms[_length++], &pcm, sizeof(*_pcms));

	}

	*pcms = _pcms;
	*length = _length;

	goto success;

fail:
	if (_pcms != NULL)
		free(_pcms);
	rv = FALSE;

success:
	if (rep != NULL)
		dbus_message_unref(rep);
	dbus_message_unref(msg);
	return rv;
}

dbus_bool_t bluealsa_dbus_get_pcm(
		struct ba_dbus_ctx *ctx,
		const bdaddr_t *addr,
		unsigned int transports,
		unsigned int mode,
		struct ba_pcm *pcm,
		DBusError *error) {

	const bool get_last = bacmp(addr, BDADDR_ANY) == 0;
	struct ba_pcm *pcms = NULL;
	struct ba_pcm *match = NULL;
	dbus_bool_t rv = TRUE;
	size_t length = 0;
	uint32_t seq = 0;
	size_t i;

	if (!bluealsa_dbus_get_pcms(ctx, &pcms, &length, error))
		return FALSE;

	for (i = 0; i < length; i++) {
		if (get_last) {
			if (pcms[i].sequence >= seq &&
					pcms[i].transport & transports &&
					pcms[i].mode == mode) {
				seq = pcms[i].sequence;
				match = &pcms[i];
			}
		}
		else if (bacmp(&pcms[i].addr, addr) == 0 &&
				pcms[i].transport & transports &&
				pcms[i].mode == mode) {
			match = &pcms[i];
			break;
		}
	}

	if (match != NULL)
		memcpy(pcm, match, sizeof(*pcm));
	else {
		dbus_set_error(error, DBUS_ERROR_FILE_NOT_FOUND, "PCM not found");
		rv = FALSE;
	}

	free(pcms);
	return rv;
}

/**
 * Open BlueALSA RFCOMM socket for dispatching AT commands. */
dbus_bool_t bluealsa_dbus_open_rfcomm(
		struct ba_dbus_ctx *ctx,
		const char *rfcomm_path,
		int *fd_rfcomm,
		DBusError *error) {

	DBusMessage *msg;
	if ((msg = dbus_message_new_method_call(ctx->ba_service, rfcomm_path,
					BLUEALSA_INTERFACE_RFCOMM, "Open")) == NULL) {
		dbus_set_error(error, DBUS_ERROR_NO_MEMORY, NULL);
		return FALSE;
	}

	DBusMessage *rep;
	if ((rep = dbus_connection_send_with_reply_and_block(ctx->conn,
					msg, DBUS_TIMEOUT_USE_DEFAULT, error)) == NULL) {
		dbus_message_unref(msg);
		return FALSE;
	}

	dbus_bool_t rv;
	rv = dbus_message_get_args(rep, error,
			DBUS_TYPE_UNIX_FD, fd_rfcomm,
			DBUS_TYPE_INVALID);

	dbus_message_unref(rep);
	dbus_message_unref(msg);
	return rv;
}

/**
 * Call the given function for each key/value pairs. */
dbus_bool_t bluealsa_dbus_message_iter_dict(
		DBusMessageIter *iter,
		DBusError *error,
		dbus_bool_t (*cb)(const char *key, DBusMessageIter *val, void *data, DBusError *err),
		void *userdata) {

	char *signature;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		goto fail;

	DBusMessageIter iter_dict;
	for (dbus_message_iter_recurse(iter, &iter_dict);
			dbus_message_iter_get_arg_type(&iter_dict) != DBUS_TYPE_INVALID;
			dbus_message_iter_next(&iter_dict)) {

		DBusMessageIter iter_entry;
		const char *key;

		if (dbus_message_iter_get_arg_type(&iter_dict) != DBUS_TYPE_DICT_ENTRY)
			goto fail;
		dbus_message_iter_recurse(&iter_dict, &iter_entry);
		if (dbus_message_iter_get_arg_type(&iter_entry) != DBUS_TYPE_STRING)
			goto fail;
		dbus_message_iter_get_basic(&iter_entry, &key);
		if (!dbus_message_iter_next(&iter_entry))
			goto fail;

		if (!cb(key, &iter_entry, userdata, error))
			return FALSE;

	}

	return TRUE;

fail:
	signature = dbus_message_iter_get_signature(iter);
	dbus_set_error(error, DBUS_ERROR_INVALID_SIGNATURE,
			"Incorrect signature: %s != a{s#}", signature);
	dbus_free(signature);
	return FALSE;
}

/**
 * Parse BlueALSA PCM. */
dbus_bool_t bluealsa_dbus_message_iter_get_pcm(
		DBusMessageIter *iter,
		DBusError *error,
		struct ba_pcm *pcm) {

	const char *path;
	char *signature;

	memset(pcm, 0, sizeof(*pcm));

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_OBJECT_PATH)
		goto fail;
	dbus_message_iter_get_basic(iter, &path);

	if (!dbus_message_iter_next(iter))
		goto fail;

	DBusMessageIter iter_ifaces;
	for (dbus_message_iter_recurse(iter, &iter_ifaces);
			dbus_message_iter_get_arg_type(&iter_ifaces) != DBUS_TYPE_INVALID;
			dbus_message_iter_next(&iter_ifaces)) {

		if (dbus_message_iter_get_arg_type(&iter_ifaces) != DBUS_TYPE_DICT_ENTRY)
			goto fail;

		DBusMessageIter iter_iface_entry;
		dbus_message_iter_recurse(&iter_ifaces, &iter_iface_entry);

		const char *iface_name;
		if (dbus_message_iter_get_arg_type(&iter_iface_entry) != DBUS_TYPE_STRING)
			goto fail;
		dbus_message_iter_get_basic(&iter_iface_entry, &iface_name);

		if (strcmp(iface_name, BLUEALSA_INTERFACE_PCM) == 0) {

			strncpy(pcm->pcm_path, path, sizeof(pcm->pcm_path) - 1);

			if (!dbus_message_iter_next(&iter_iface_entry))
				goto fail;

			DBusError err = DBUS_ERROR_INIT;
			if (!bluealsa_dbus_message_iter_get_pcm_props(&iter_iface_entry, &err, pcm)) {
				dbus_set_error(error, err.name, "Get properties: %s", err.message);
				dbus_error_free(&err);
				return FALSE;
			}

			break;
		}

	}

	return TRUE;

fail:
	signature = dbus_message_iter_get_signature(iter);
	dbus_set_error(error, DBUS_ERROR_INVALID_SIGNATURE,
			"Incorrect signature: %s != oa{sa{sv}}", signature);
	dbus_free(signature);
	return FALSE;
}

/**
 * Callback function for BlueALSA PCM properties parser. */
static dbus_bool_t bluealsa_dbus_message_iter_get_pcm_props_cb(const char *key,
		DBusMessageIter *value, void *userdata, DBusError *error) {
	struct ba_pcm *pcm = (struct ba_pcm *)userdata;

	char type;
	if ((type = dbus_message_iter_get_arg_type(value)) != DBUS_TYPE_VARIANT) {
		dbus_set_error(error, DBUS_ERROR_INVALID_SIGNATURE,
				"Incorrect property value type: %c != %c", type, DBUS_TYPE_VARIANT);
		return FALSE;
	}

	DBusMessageIter variant;
	dbus_message_iter_recurse(value, &variant);
	type = dbus_message_iter_get_arg_type(&variant);

	char type_expected;
	const char *tmp;

	if (strcmp(key, "Device") == 0) {
		if (type != (type_expected = DBUS_TYPE_OBJECT_PATH))
			goto fail;
		dbus_message_iter_get_basic(&variant, &tmp);
		strncpy(pcm->device_path, tmp, sizeof(pcm->device_path) - 1);
		path2ba(tmp, &pcm->addr);
	}
	else if (strcmp(key, "Sequence") == 0) {
		if (type != (type_expected = DBUS_TYPE_UINT32))
			goto fail;
		dbus_message_iter_get_basic(&variant, &pcm->sequence);
	}
	else if (strcmp(key, "Transport") == 0) {
		if (type != (type_expected = DBUS_TYPE_STRING))
			goto fail;
		dbus_message_iter_get_basic(&variant, &tmp);
		if (strstr(tmp, "A2DP-source") != NULL)
			pcm->transport = BA_PCM_TRANSPORT_A2DP_SOURCE;
		else if (strstr(tmp, "A2DP-sink") != NULL)
			pcm->transport = BA_PCM_TRANSPORT_A2DP_SINK;
		else if (strstr(tmp, "HFP-AG") != NULL)
			pcm->transport = BA_PCM_TRANSPORT_HFP_AG;
		else if (strstr(tmp, "HFP-HF") != NULL)
			pcm->transport = BA_PCM_TRANSPORT_HFP_HF;
		else if (strstr(tmp, "HSP-AG") != NULL)
			pcm->transport = BA_PCM_TRANSPORT_HSP_AG;
		else if (strstr(tmp, "HSP-HS") != NULL)
			pcm->transport = BA_PCM_TRANSPORT_HSP_HS;
	}
	else if (strcmp(key, "Mode") == 0) {
		if (type != (type_expected = DBUS_TYPE_STRING))
			goto fail;
		dbus_message_iter_get_basic(&variant, &tmp);
		if (strcmp(tmp, "source") == 0)
			pcm->mode = BA_PCM_MODE_SOURCE;
		else if (strcmp(tmp, "sink") == 0)
			pcm->mode = BA_PCM_MODE_SINK;
	}

	return TRUE;

fail:
	dbus_set_error(error, DBUS_ERROR_INVALID_SIGNATURE,
			"Incorrect variant for '%s': %c != %c", key, type, type_expected);
	return FALSE;
}

/**
 * Parse BlueALSA PCM properties. */
dbus_bool_t bluealsa_dbus_message_iter_get_pcm_props(
		DBusMessageIter *iter,
		DBusError *error,
		struct ba_pcm *pcm) {
	return bluealsa_dbus_message_iter_dict(iter, error,
			bluealsa_dbus_message_iter_get_pcm_props_cb, pcm);
}
