/*
 * hfpag-session.c
 * Copyright (c) 2023 @borine (https://github.com/borine/)
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hfpag-session.h"

#define BLUEALSA_HFPAG_MUTEX_OFFSET 0
#define BLUEALSA_HFPAG_FLAG_OFFSET 1

static const char *hfpag_transfer_call[] = {
	"\r\n+CIEV:1,1\r\n",
	"\r\n+CIEV:5,5\r\n",
	"\r\n+CIEV:2,1\r\n",
	NULL,
};

static const char *hfpag_terminate_call[] = {
	"\r\n+CIEV:2,0\r\n",
	"\r\n+CIEV:5,0\r\n",
	"\r\n+CIEV:1,0\r\n",
	NULL,
};

static void send_rfcomm_sequence(struct ba_dbus_ctx *dbus_ctx, const char *rfcomm_path, const char **commands) {
	int rfcomm_fd;
	DBusError err = DBUS_ERROR_INIT;
	if (!bluealsa_dbus_open_rfcomm(dbus_ctx, rfcomm_path, &rfcomm_fd, &err)) {
		SNDERR("Couldn't open RFCOMM: %s", err.message);
		dbus_error_free(&err);
		return;
	}

	int n;
	for (n = 0; commands[n] != NULL; n++) {
		ssize_t err = write(rfcomm_fd, commands[n], strlen(commands[n]));
		if (err < 0 || (size_t)err < strlen(commands[n])) {
			SNDERR("Couldn't complete RFCOMM sequence: %s", strerror(errno));
			break;
		}
	}

	close(rfcomm_fd);
}

static const char *get_lock_dir(void) {

	/* If /dev/shm is available and usable, we prefer it. */
	char *lockdir = "/dev/shm";
	if (faccessat(0, lockdir, R_OK|W_OK, AT_EACCESS) < 0) {
		/* FIXME - if the capture and playback applications run in different
		 * environments they may not see the same lock file. We really need a
		 * more reliable way of agreeing a path for the lock file when /dev/shm
		 * cannot be used. */
		lockdir = getenv("XDG_RUNTIME_DIR");
		if (lockdir == NULL) {
			lockdir = getenv("TMPDIR");
			if (lockdir == NULL)
				lockdir = "/tmp";
		}
	}

	return lockdir;
}

int hfpag_session_init(struct hfpag_session **phfpag, const char *device_path, const bdaddr_t *addr) {

	if (strlen(device_path) < 37) {
		SNDERR("Invalid PCM device path");
		return -EINVAL;
	}

	struct hfpag_session *hfpag = malloc(sizeof(struct hfpag_session));
	if (hfpag == NULL)
		return -ENOMEM;

	const char *dev_path = device_path + 11;
	snprintf(hfpag->rfcomm_path, sizeof(hfpag->rfcomm_path), "/org/bluealsa/%s/rfcomm", dev_path);

	snprintf(hfpag->lock_file, PATH_MAX,
			"%s/bahfpag%.2X%.2X%.2X%.2X%.2X%.2X.lock",
			get_lock_dir(),
			addr->b[5], addr->b[4], addr->b[3],
			addr->b[2], addr->b[1], addr->b[0]);

	hfpag->lock_fd = -1;

	*phfpag = hfpag;
	return 0;
}

/**
 * An HFP device has 2 PCMs (playback and capture), so we need to ensure that
 * only the first one opened sends the RFCOMM call transfer sequence, and only
 * the last one closed sends the RFCOMM call termination sequence. We use Linux-
 * specific Open File Descriptor Locking to achieve this, since neither POSIX
 * nor BSD file locks have the necessary semantics.
 */
int hfpag_session_begin(struct hfpag_session *hfpag, struct ba_dbus_ctx *dbus_ctx) {
	/* lock to ensure exclusive access to the call session state. */
	struct flock mutex_lock = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start = BLUEALSA_HFPAG_MUTEX_OFFSET,
		.l_len = 1,
	};

	/* shared lock held continuously while call session is active. */
	struct flock flag_lock = {
		.l_type = F_RDLCK,
		.l_whence = SEEK_SET,
		.l_start = BLUEALSA_HFPAG_FLAG_OFFSET,
		.l_len = 1,
	};

	int fd;
	int err;
	int retries = 5;
	while (retries > 0) {
		fd = open(hfpag->lock_file, O_CREAT|O_CLOEXEC|O_RDWR, S_IRUSR|S_IWUSR);
		if (fd == -1) {
			SNDERR("Unable to open lock file");
			return -1;
		}

		/* Wait for mutex lock before managing call state. */
		err = fcntl(fd, F_OFD_SETLKW, &mutex_lock);
		if (err == -1) {
			SNDERR("Unable to set lock file");
			close(fd);
			return -1;
		}

		/* There is a chance that the lock file was unlinked while this process
		 * was waiting for the mutex. In that case the descriptor fd is no
		 * longer referring to the correct file. So we check that we are indeed
		 * looking at the correct file path by comparing the inode of the fd
		 * with the current inode of the lock file path. */
		struct stat fd_stat;
		if (fstat(fd, &fd_stat) < 0) {
			SNDERR("Unable to check lock file");
			close(fd);
			return -1;
		}
		struct stat path_stat;
		err = stat(hfpag->lock_file, &path_stat);
		if (err < 0 && errno != ENOENT) {
			SNDERR("Unable to check lock file");
			close(fd);
			return -1;
		}
		if (errno == ENOENT || fd_stat.st_ino != path_stat.st_ino) {
			/* The lock file we opened is no longer valid - try again. */
			close(fd);
			fd = -1;
			--retries;
			continue;
		}

		break;
	}
	if (fd == -1) {
		SNDERR("Unable to open lock file - maximum retries exceeded");
		return -1;
	}

	/* set flag lock to indicate we are using this HFP device. */
	err = fcntl(fd, F_OFD_SETLKW, &flag_lock);
	if (err == -1) {
		SNDERR("Unable to set lock file");
		close(fd);
		return -1;
	}

	/* test if we can switch flag to an exclusive lock - if so no other process
	 * (or thread) is using this HFP device. */
	flag_lock.l_type = F_WRLCK;
	err = fcntl(fd, F_OFD_SETLK, &flag_lock);
	if (err == -1) {
		if (errno != EAGAIN) {
			SNDERR("Unable to test lock file");
			close(fd);
			return -1;
		}
	}
	else {
		/* We are (currently) the only process using this HFP device */
		send_rfcomm_sequence(dbus_ctx, hfpag->rfcomm_path, hfpag_transfer_call);

		/* Revert the flag to a shared lock */
		flag_lock.l_type = F_RDLCK;
		err = fcntl(fd, F_OFD_SETLK, &flag_lock);
	}

	/* Release the mutex lock. */
	mutex_lock.l_type = F_UNLCK;
	err = fcntl(fd, F_OFD_SETLK, &mutex_lock);
	if (err == -1) {
		SNDERR("Unable to release lock file");
		close(fd);
		return -1;
	}

	hfpag->lock_fd = fd;
	return 0;
}

int hfpag_session_end(struct hfpag_session *hfpag, struct ba_dbus_ctx *dbus_ctx) {
	struct flock mutex_lock = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start = BLUEALSA_HFPAG_MUTEX_OFFSET,
		.l_len = 1,
	};

	struct flock flag_lock = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start = BLUEALSA_HFPAG_FLAG_OFFSET,
		.l_len = 1,
	};

	if (hfpag->lock_fd == -1)
		return 0;

	int ret = 0;

	/* Wait for mutex lock before managing call state. */
	int err = fcntl(hfpag->lock_fd, F_OFD_SETLKW, &mutex_lock);
	if (err == -1) {
		SNDERR("Unable to set lock file");
		ret = -1;
		goto finish;
	}

	/* test if we can switch the flag to an exclusive lock - if so no other
	 * process (or thread) is using this HFP device. */
	err = fcntl(hfpag->lock_fd, F_OFD_SETLK, &flag_lock);
	if (err == -1) {
		if (errno != EAGAIN) {
			SNDERR("Unable to test lock file");
			ret = -1;
			goto finish;
		}
	}
	else {
		/* We are (currently) the only process using this HFP device */
		send_rfcomm_sequence(dbus_ctx, hfpag->rfcomm_path, hfpag_terminate_call);
		unlink(hfpag->lock_file);
	}

finish:
	/* closing the lock file automatically releases all locks */
	close(hfpag->lock_fd);
	hfpag->lock_fd = -1;
	return ret;
}

void hfpag_session_free(struct hfpag_session *hfpag) {
	if (hfpag->lock_fd >= 0) {
		close(hfpag->lock_fd);
		hfpag->lock_fd = -1;
	}
	free(hfpag);
}

