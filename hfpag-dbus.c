/*
 * hfpag-dbus.c
 * Copyright (c) 2024 @borine (https://github.com/borine/)
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

struct pcm_data {
	bdaddr_t addr;
	unsigned int transport;
	char device_path[128];
	uint32_t sequence;
};

struct rfcomm_data {
	const char *path;
	const char *addr;
};

static dbus_bool_t dbus_connection_ctx_init(
		struct ba_dbus_ctx *ctx,
		const char *ba_service_name,
		DBusError *error) {
			
	memset(ctx, 0, sizeof(*ctx));

	if ((ctx->conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, error)) == NULL)
		return FALSE;

	/* do not terminate in case of D-Bus connection being lost */
	dbus_connection_set_exit_on_disconnect(ctx->conn, FALSE);

}

void ba_dbus_connection_ctx_free(
		struct ba_dbus_ctx *ctx) {
	if (ctx->conn != NULL) {
		dbus_connection_close(ctx->conn);
		dbus_connection_unref(ctx->conn);
		ctx->conn = NULL;
	}
}

static bool get_all_pcms(const char *service, struct ba_pcm **pcms, size_t *count) {

	DBusConnection *conn;
	DBusMessage *msg, *rep;
	DBusError error = DBUS_ERROR_INIT;
	struct pcm_data pcm;

	if ((conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error)) == NULL)
		return false;
		
	if ((msg = dbus_message_new_method_call(service, "/org/bluealsa",
				DBUS_INTERFACE_OBJECT_MANAGER, "GetManagedObjects")) == NULL) {
		dbus_set_error_const(&error, DBUS_ERROR_NO_MEMORY, NULL);
		return false;
	}

	dbus_bool_t rv = TRUE;
	struct ba_pcm *_pcms = NULL;
	size_t _length = 0;

	if ((rep = dbus_connection_send_with_reply_and_block(conn,
					msg, DBUS_TIMEOUT_USE_DEFAULT, &error)) == NULL)
		goto fail;

	DBusMessageIter iter_rep;
	if (!dbus_message_iter_init(rep, &iter_rep)) {
		dbus_set_error(&error, DBUS_ERROR_INVALID_SIGNATURE, "Empty response message");
		goto fail;
	}

	DBusMessageIter iter_match;

// ARRAY of DICT_ENTRY<OBJPATH,ARRAY of DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>>

	DBusMessageIter iter_objects;
	for (dbus_message_iter_recurse(&iter_rep, &iter_objects);
			dbus_message_iter_get_arg_type(&iter_objects) != DBUS_TYPE_INVALID;
			dbus_message_iter_next(&iter_objects)) {

		if (dbus_message_iter_get_arg_type(&iter_objects) != DBUS_TYPE_DICT_ENTRY)
			goto fail;

// DICT_ENTRY<OBJPATH,ARRAY of DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>>

		DBusMessageIter iter_object_entry;
		dbus_message_iter_recurse(&iter_objects, &iter_object_entry);

		if (dbus_message_iter_get_arg_type(&iter_object_entry) != DBUS_TYPE_OBJECT_PATH)
			goto fail;
		if (!dbus_message_iter_next(&iter_object_entry))
			goto fail;

// ARRAY of DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>

		DBusMessageIter iter_interfaces;
		for (dbus_message_iter_recurse(&iter_object_entry, &iter_interfaces);
				dbus_message_iter_get_arg_type(&iter_interfaces) != DBUS_TYPE_INVALID;
				dbus_message_iter_next(&iter_interfaces)) {

			if (dbus_message_iter_get_arg_type(&iter_interfaces) != DBUS_TYPE_DICT_ENTRY)
				goto fail;

// DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>

			DBusMessageIter iter_interface_entry;
			dbus_message_iter_recurse(&iter_interfaces, &iter_interface_entry);
			if (dbus_message_iter_get_arg_type(&iter_interface_entry) != DBUS_TYPE_STRING)
				goto fail;
// test that interface name is "org.bluealsa.PCM1" here, or continue

			if (!dbus_message_iter_next(&iter_interface_entry))
				goto fail;
		}
		
// ARRAY of DICT_ENTRY<STRING,VARIANT>

			DBusMessageIter iter_properties;
			for (dbus_message_iter_recurse(&iter_interfaces, &iter_properties);
					dbus_message_iter_get_arg_type(&iter_properties) != DBUS_TYPE_INVALID;
					dbus_message_iter_next(&iter_properties)) {

				if (dbus_message_iter_get_arg_type(&iter_properties) != DBUS_TYPE_DICT_ENTRY)
					goto fail;


// DICT_ENTRY<STRING,VARIANT>

				DBusMessageIter iter_property_entry;
				dbus_message_iter_recurse(&iter_properties, &iter_property_entry);
				if (dbus_message_iter_get_arg_type(&iter_property_entry) != DBUS_TYPE_STRING)
					goto fail;

// get property name into local var
				char *property;
				dbus_message_iter_get_basic(&iter_property_entry, &property);
				if (!dbus_message_iter_next(&iter_property_entry))
					goto fail;
				if (dbus_message_iter_get_arg_type(&iter_property_entry) != DBUS_TYPE_VARIANT)
					goto fail;

				DBusMessageIter variant;
				dbus_message_iter_recurse(&iter_property_entry, &variant);
				type = dbus_message_iter_get_arg_type(&variant);

				if (strcmp(property, "") == 0) {
					
				}
				else if (strcmp(property, "Transport") == 0) {
					
					
				}
				else if (strcmp(property, "Sequence") == 0) {
					
					
				}
				else if (strcmp(property, "Device") == 0) {
					if (type != DBUS_TYPE_OBJECT_PATH)
						goto fail;
					dbus_message_iter_get_basic(&variant, &tmp);
					strncpy(pcm->device_path, tmp, sizeof(pcm->device_path) - 1);
					path2ba(tmp, &pcm->addr);
					
				}
				else
					continue;
					
// get property val into variant


		if (!dbus_message_iter_get_ba_pcm(&iter_object_entry, &err, &pcm)) {
			dbus_set_error(error, err.name, "Get PCM: %s", err.message);
			dbus_error_free(&err);
			goto fail;
		}

		if (!(ba_pcm.transport & BA_PCM_TRANSPORT_HFP_AG)
			continue;

		struct ba_pcm *tmp = _pcms;
		if ((tmp = realloc(tmp, (_length + 1) * sizeof(*tmp))) == NULL) {
			dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, NULL);
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

static struct rfcomm_data *find_rfcomm(DBusMessageIter );

static DBusMessageIter *find_interface(DBusMessageIter* iter_rep, const char *interface, const char* addr) {
	DBusMessageIter iter_objects;
	for (dbus_message_iter_recurse(iter_rep, &iter_objects);
			dbus_message_iter_get_arg_type(&iter_objects) != DBUS_TYPE_INVALID;
			dbus_message_iter_next(&iter_objects)) {

		if (dbus_message_iter_get_arg_type(&iter_objects) != DBUS_TYPE_DICT_ENTRY)
			return NULL;

// DICT_ENTRY<OBJPATH,ARRAY of DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>>

		DBusMessageIter iter_object_entry;
		dbus_message_iter_recurse(&iter_objects, &iter_object_entry);

		if (dbus_message_iter_get_arg_type(&iter_object_entry) != DBUS_TYPE_OBJECT_PATH)
			return NULL;
		if (!dbus_message_iter_next(&iter_object_entry))
			return NULL;

// ARRAY of DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>

		DBusMessageIter iter_interfaces;
		for (dbus_message_iter_recurse(&iter_object_entry, &iter_interfaces);
				dbus_message_iter_get_arg_type(&iter_interfaces) != DBUS_TYPE_INVALID;
				dbus_message_iter_next(&iter_interfaces)) {

			if (dbus_message_iter_get_arg_type(&iter_interfaces) != DBUS_TYPE_DICT_ENTRY)
				return NULL;

// DICT_ENTRY<STRING,ARRAY of DICT_ENTRY<STRING,VARIANT>>

			DBusMessageIter iter_interface_entry;
			dbus_message_iter_recurse(&iter_interfaces, &iter_interface_entry);
			if (dbus_message_iter_get_arg_type(&iter_interface_entry) != DBUS_TYPE_STRING)
				return NULL;
				
// test that interface name here, or continue
			char *tmp;
			dbus_message_iter_get_basic(&iter_property_entry, &tmp);
			if (strcmp(interface, tmp) != 0)
				continue;
				
			if (!dbus_message_iter_next(&iter_property_entry))
				return NULL;
			
			return 



}
