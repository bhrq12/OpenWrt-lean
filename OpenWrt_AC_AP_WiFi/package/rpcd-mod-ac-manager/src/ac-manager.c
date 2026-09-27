/*
 * rpcd-mod-ac-manager - ubus bridge plugin for AC Manager
 *
 * Provides read-only ubus interfaces for:
 *   - ac-manager.status.get     (service status, PID, version)
 *   - ac-manager.devices.list   (AP device list from SQLite)
 *   - ac-manager.devices.get    (single device detail)
 *   - ac-manager.templates.list (config template list from SQLite)
 *   - ac-manager.templates.get  (single template detail)
 *   - ac-manager.logs.read      (audit log entries from log file)
 *
 * Copyright (C) 2026 AC+AP WiFi Management System
 * Licensed under GPL-2.0
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>

#include <libubus.h>
#include <libubox/blob.h>
#include <libubox/blobmsg.h>
#include <libubox/uloop.h>

#include <rpcd/plugin.h>

#include <sqlite3.h>

/* Database path candidates - try in order */
#define DB_PATH_1 "/var/lib/ac-manager/ac-manager.db"
#define DB_PATH_2 "/var/lib/ac-manager/ac.db"

/* Log file path */
#define LOG_FILE "/var/log/ac-manager.log"

/* Max log entries to return */
#define MAX_LOG_ENTRIES 200

static struct blob_buf blob;

/*============================================================
 * Helper functions
 *============================================================*/

/* Find ac-manager process PID by scanning /proc */
static int
find_ac_manager_pid(void)
{
	DIR *d = opendir("/proc");
	struct dirent *de;
	int pid = -1;

	if (!d)
		return -1;

	while ((de = readdir(d)) != NULL) {
		if (de->d_type != DT_DIR)
			continue;

		int n = atoi(de->d_name);
		if (n <= 0)
			continue;

		char path[64];
		char comm[256];
		FILE *f;

		snprintf(path, sizeof(path), "/proc/%d/comm", n);
		f = fopen(path, "r");
		if (!f)
			continue;

		if (fgets(comm, sizeof(comm), f)) {
			comm[strcspn(comm, "\n")] = 0;
			/* Check if process name is "ac-manager" */
			if (strcmp(comm, "ac-manager") == 0) {
				pid = n;
				fclose(f);
				break;
			}
		}
		fclose(f);
	}

	closedir(d);
	return pid;
}

/* Get process uptime in seconds from /proc/<pid>/stat */
static long
get_process_uptime(int pid)
{
	if (pid <= 0)
		return 0;

	char path[64];
	FILE *f;
	unsigned long starttime = 0;
	long clk_tck = sysconf(_SC_CLK_TCK);

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	f = fopen(path, "r");
	if (!f)
		return 0;

	/* /proc/<pid>/stat format: pid (comm) state ppid ... starttime (field 22) */
	/* Skip fields 1-21, read field 22 (starttime) */
	char buf[1024];
	if (fgets(buf, sizeof(buf), f)) {
		char *p = strrchr(buf, ')');
		if (p) {
			int field = 2; /* state field after (comm) */
			p++;
			while (field < 22 && p) {
				p = strchr(p, ' ');
				if (p) { p++; field++; }
			}
			if (p) {
				sscanf(p, "%lu", &starttime);
			}
		}
	}
	fclose(f);

	if (starttime == 0 || clk_tck <= 0)
		return 0;

	/* Calculate uptime */
	FILE *uptime_f = fopen("/proc/uptime", "r");
	double sys_uptime = 0;
	if (uptime_f) {
		if (fscanf(uptime_f, "%lf", &sys_uptime))
			; /* read ok */
		fclose(uptime_f);
	}

	if (sys_uptime > 0) {
		double proc_start = (double)starttime / clk_tck;
		return (long)(sys_uptime - proc_start);
	}

	return 0;
}

/* Try to open SQLite database read-only */
static sqlite3 *
open_db_readonly(void)
{
	sqlite3 *db = NULL;
	int rc;

	/* Try first path */
	rc = sqlite3_open_v2(DB_PATH_1, &db,
		SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
	if (rc == SQLITE_OK)
		return db;

	if (db)
		sqlite3_close(db);

	/* Try second path */
	rc = sqlite3_open_v2(DB_PATH_2, &db,
		SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
	if (rc == SQLITE_OK)
		return db;

	if (db)
		sqlite3_close(db);

	return NULL;
}

/*============================================================
 * ubus object: ac-manager.status
 *============================================================*/

static int
handle_status_get(struct ubus_context *ctx, struct ubus_object *obj,
                   struct ubus_request_data *req, const char *method,
                   struct blob_attr *msg)
{
	int pid = find_ac_manager_pid();
	int running = (pid > 0) ? 1 : 0;
	long uptime = get_process_uptime(pid);
	int online_count = 0;
	int total_count = 0;

	/* Try to get device counts from SQLite */
	sqlite3 *db = open_db_readonly();
	if (db) {
		sqlite3_stmt *stmt;
		/* Online devices (status = 1) */
		if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM devices WHERE status=1",
		                       -1, &stmt, NULL) == SQLITE_OK) {
			if (sqlite3_step(stmt) == SQLITE_ROW)
				online_count = sqlite3_column_int(stmt, 0);
			sqlite3_finalize(stmt);
		}
		/* Total devices */
		if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM devices",
		                       -1, &stmt, NULL) == SQLITE_OK) {
			if (sqlite3_step(stmt) == SQLITE_ROW)
				total_count = sqlite3_column_int(stmt, 0);
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
	}

	/* Get WS port from config file */
	int ws_port = 8080;
	FILE *f = fopen("/etc/ac-manager/ac-manager.conf", "r");
	if (f) {
		char line[256];
		while (fgets(line, sizeof(line), f)) {
			int val;
			if (sscanf(line, " port = %d", &val) == 1) {
				ws_port = val;
				break;
			}
		}
		fclose(f);
	}

	blob_buf_init(&blob, 0);
	blobmsg_add_u8(&blob, "running", running);
	blobmsg_add_u32(&blob, "pid", (uint32_t)pid);
	blobmsg_add_string(&blob, "version", "1.0.0");
	blobmsg_add_u32(&blob, "uptime", (uint32_t)uptime);
	blobmsg_add_u32(&blob, "ws_port", (uint32_t)ws_port);
	blobmsg_add_u32(&blob, "online_devices", (uint32_t)online_count);
	blobmsg_add_u32(&blob, "total_devices", (uint32_t)total_count);
	/* ws_connections unknown without direct ubus to ac-manager */
	blobmsg_add_u32(&blob, "ws_connections", 0);

	ubus_send_reply(ctx, req, blob.head);
	return 0;
}

/*============================================================
 * ubus object: ac-manager.devices
 *============================================================*/

static const struct blobmsg_policy devices_list_policy[] = {
	[0] = { .name = "site_id", .type = BLOBMSG_TYPE_STRING },
	[1] = { .name = "status", .type = BLOBMSG_TYPE_INT32 },
};

static int
handle_devices_list(struct ubus_context *ctx, struct ubus_object *obj,
                     struct ubus_request_data *req, const char *method,
                     struct blob_attr *msg)
{
	struct blob_attr *tb[2];
	const char *site_id = NULL;
	int status_filter = -1;

	blobmsg_parse(devices_list_policy, ARRAY_SIZE(devices_list_policy),
	              tb, blobmsg_data(msg), blobmsg_data_len(msg));
	if (tb[0])
		site_id = blobmsg_get_string(tb[0]);
	if (tb[1])
		status_filter = blobmsg_get_u32(tb[1]);

	sqlite3 *db = open_db_readonly();
	if (!db) {
		blob_buf_init(&blob, 0);
		blobmsg_add_u8(&blob, "error", 1);
		blobmsg_add_string(&blob, "message", "Database not available");
		ubus_send_reply(ctx, req, blob.head);
		return UBUS_STATUS_SYSTEM_ERROR;
	}

	sqlite3_busy_timeout(db, 2000);

	char sql[512];
	const char *base = "SELECT dev_sn, dev_model, fw_version, site_id, "
	                   "group_id, status, cur_config_id, config_version, "
	                   "register_time, last_heartbeat "
	                   "FROM devices";

	if (site_id && status_filter >= 0) {
		snprintf(sql, sizeof(sql),
			"%s WHERE site_id='%s' AND status=%d", base, site_id, status_filter);
	} else if (site_id) {
		snprintf(sql, sizeof(sql),
			"%s WHERE site_id='%s'", base, site_id);
	} else if (status_filter >= 0) {
		snprintf(sql, sizeof(sql),
			"%s WHERE status=%d", base, status_filter);
	} else {
		snprintf(sql, sizeof(sql), "%s", base);
	}

	/* Sanitize: ensure no injection in site_id */
	if (site_id) {
		/* Basic sanitization - reject if contains quotes */
		for (const char *p = site_id; *p; p++) {
			if (*p == '\'' || *p == ';' || *p == '-' || *p == '"') {
				strcpy(sql, base);
				break;
			}
		}
	}

	sqlite3_stmt *stmt;
	blob_buf_init(&blob, 0);
	void *arr = blobmsg_open_array(&blob, "devices");

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			void *tbl = blobmsg_open_table(&blob, NULL);
			blobmsg_add_string(&blob, "dev_sn",
				(const char*)sqlite3_column_text(stmt, 0));
			blobmsg_add_string(&blob, "dev_model",
				(const char*)sqlite3_column_text(stmt, 1));
			blobmsg_add_string(&blob, "fw_version",
				(const char*)sqlite3_column_text(stmt, 2));
			blobmsg_add_string(&blob, "site_id",
				(const char*)sqlite3_column_text(stmt, 3));
			blobmsg_add_string(&blob, "group_id",
				(const char*)sqlite3_column_text(stmt, 4));
			blobmsg_add_u32(&blob, "status",
				(uint32_t)sqlite3_column_int(stmt, 5));
			blobmsg_add_string(&blob, "cur_config_id",
				(const char*)sqlite3_column_text(stmt, 6));
			blobmsg_add_u32(&blob, "config_version",
				(uint32_t)sqlite3_column_int(stmt, 7));
			blobmsg_add_u32(&blob, "register_time",
				(uint32_t)sqlite3_column_int(stmt, 8));
			blobmsg_add_u32(&blob, "last_heartbeat",
				(uint32_t)sqlite3_column_int(stmt, 9));
			blobmsg_close_table(&blob, tbl);
		}
		sqlite3_finalize(stmt);
	}
	blobmsg_close_array(&blob, arr);

	sqlite3_close(db);
	ubus_send_reply(ctx, req, blob.head);
	return 0;
}

static const struct blobmsg_policy device_get_policy[] = {
	[0] = { .name = "dev_sn", .type = BLOBMSG_TYPE_STRING },
};

static int
handle_device_get(struct ubus_context *ctx, struct ubus_object *obj,
                   struct ubus_request_data *req, const char *method,
                   struct blob_attr *msg)
{
	struct blob_attr *tb[1];
	blobmsg_parse(device_get_policy, ARRAY_SIZE(device_get_policy),
	              tb, blobmsg_data(msg), blobmsg_data_len(msg));

	if (!tb[0])
		return UBUS_STATUS_INVALID_ARGUMENT;

	const char *dev_sn = blobmsg_get_string(tb[0]);
	/* Basic sanitization */
	for (const char *p = dev_sn; *p; p++) {
		if (*p == '\'' || *p == ';' || *p == '"' || *p == '-')
			return UBUS_STATUS_INVALID_ARGUMENT;
	}

	sqlite3 *db = open_db_readonly();
	if (!db) {
		return UBUS_STATUS_SYSTEM_ERROR;
	}

	sqlite3_busy_timeout(db, 2000);

	char sql[512];
	snprintf(sql, sizeof(sql),
		"SELECT dev_sn, dev_model, fw_version, site_id, "
		"group_id, status, cur_config_id, config_version, "
		"register_time, last_heartbeat "
		"FROM devices WHERE dev_sn='%s'", dev_sn);

	sqlite3_stmt *stmt;
	blob_buf_init(&blob, 0);

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			blobmsg_add_string(&blob, "dev_sn",
				(const char*)sqlite3_column_text(stmt, 0));
			blobmsg_add_string(&blob, "dev_model",
				(const char*)sqlite3_column_text(stmt, 1));
			blobmsg_add_string(&blob, "fw_version",
				(const char*)sqlite3_column_text(stmt, 2));
			blobmsg_add_string(&blob, "site_id",
				(const char*)sqlite3_column_text(stmt, 3));
			blobmsg_add_string(&blob, "group_id",
				(const char*)sqlite3_column_text(stmt, 4));
			blobmsg_add_u32(&blob, "status",
				(uint32_t)sqlite3_column_int(stmt, 5));
			blobmsg_add_string(&blob, "cur_config_id",
				(const char*)sqlite3_column_text(stmt, 6));
			blobmsg_add_u32(&blob, "config_version",
				(uint32_t)sqlite3_column_int(stmt, 7));
			blobmsg_add_u32(&blob, "register_time",
				(uint32_t)sqlite3_column_int(stmt, 8));
			blobmsg_add_u32(&blob, "last_heartbeat",
				(uint32_t)sqlite3_column_int(stmt, 9));
		}
		sqlite3_finalize(stmt);
	}

	sqlite3_close(db);
	ubus_send_reply(ctx, req, blob.head);
	return 0;
}

/*============================================================
 * ubus object: ac-manager.templates
 *============================================================*/

static const struct blobmsg_policy templates_list_policy[] = {
	[0] = { .name = "type", .type = BLOBMSG_TYPE_INT32 },
};

static int
handle_templates_list(struct ubus_context *ctx, struct ubus_object *obj,
                       struct ubus_request_data *req, const char *method,
                       struct blob_attr *msg)
{
	struct blob_attr *tb[1];
	int type_filter = -1;

	blobmsg_parse(templates_list_policy, ARRAY_SIZE(templates_list_policy),
	              tb, blobmsg_data(msg), blobmsg_data_len(msg));
	if (tb[0])
		type_filter = blobmsg_get_u32(tb[0]);

	sqlite3 *db = open_db_readonly();
	if (!db) {
		blob_buf_init(&blob, 0);
		blobmsg_add_u8(&blob, "error", 1);
		blobmsg_add_string(&blob, "message", "Database not available");
		ubus_send_reply(ctx, req, blob.head);
		return UBUS_STATUS_SYSTEM_ERROR;
	}

	sqlite3_busy_timeout(db, 2000);

	char sql[256];
	if (type_filter >= 0)
		snprintf(sql, sizeof(sql),
			"SELECT config_id, template_name, template_type, "
			"signature, version, creator, create_time "
			"FROM config_templates WHERE template_type=%d", type_filter);
	else
		snprintf(sql, sizeof(sql),
			"SELECT config_id, template_name, template_type, "
			"signature, version, creator, create_time "
			"FROM config_templates");

	sqlite3_stmt *stmt;
	blob_buf_init(&blob, 0);
	void *arr = blobmsg_open_array(&blob, "templates");

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			void *tbl = blobmsg_open_table(&blob, NULL);
			blobmsg_add_string(&blob, "config_id",
				(const char*)sqlite3_column_text(stmt, 0));
			blobmsg_add_string(&blob, "template_name",
				(const char*)sqlite3_column_text(stmt, 1));
			blobmsg_add_u32(&blob, "template_type",
				(uint32_t)sqlite3_column_int(stmt, 2));
			blobmsg_add_string(&blob, "signature",
				(const char*)sqlite3_column_text(stmt, 3));
			blobmsg_add_u32(&blob, "version",
				(uint32_t)sqlite3_column_int(stmt, 4));
			blobmsg_add_string(&blob, "creator",
				(const char*)sqlite3_column_text(stmt, 5));
			blobmsg_add_u32(&blob, "create_time",
				(uint32_t)sqlite3_column_int(stmt, 6));
			blobmsg_close_table(&blob, tbl);
		}
		sqlite3_finalize(stmt);
	}
	blobmsg_close_array(&blob, arr);

	sqlite3_close(db);
	ubus_send_reply(ctx, req, blob.head);
	return 0;
}

static const struct blobmsg_policy template_get_policy[] = {
	[0] = { .name = "config_id", .type = BLOBMSG_TYPE_STRING },
};

static int
handle_template_get(struct ubus_context *ctx, struct ubus_object *obj,
                    struct ubus_request_data *req, const char *method,
                    struct blob_attr *msg)
{
	struct blob_attr *tb[1];
	blobmsg_parse(template_get_policy, ARRAY_SIZE(template_get_policy),
	              tb, blobmsg_data(msg), blobmsg_data_len(msg));

	if (!tb[0])
		return UBUS_STATUS_INVALID_ARGUMENT;

	const char *config_id = blobmsg_get_string(tb[0]);
	/* Basic sanitization */
	for (const char *p = config_id; *p; p++) {
		if (*p == '\'' || *p == ';' || *p == '"' || *p == '-')
			return UBUS_STATUS_INVALID_ARGUMENT;
	}

	sqlite3 *db = open_db_readonly();
	if (!db)
		return UBUS_STATUS_SYSTEM_ERROR;

	sqlite3_busy_timeout(db, 2000);

	char sql[512];
	snprintf(sql, sizeof(sql),
		"SELECT config_id, template_name, template_type, "
		"config_content, signature, version, creator, create_time "
		"FROM config_templates WHERE config_id='%s'", config_id);

	sqlite3_stmt *stmt;
	blob_buf_init(&blob, 0);

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			blobmsg_add_string(&blob, "config_id",
				(const char*)sqlite3_column_text(stmt, 0));
			blobmsg_add_string(&blob, "template_name",
				(const char*)sqlite3_column_text(stmt, 1));
			blobmsg_add_u32(&blob, "template_type",
				(uint32_t)sqlite3_column_int(stmt, 2));
			blobmsg_add_string(&blob, "config_content",
				(const char*)sqlite3_column_text(stmt, 3));
			blobmsg_add_string(&blob, "signature",
				(const char*)sqlite3_column_text(stmt, 4));
			blobmsg_add_u32(&blob, "version",
				(uint32_t)sqlite3_column_int(stmt, 5));
			blobmsg_add_string(&blob, "creator",
				(const char*)sqlite3_column_text(stmt, 6));
			blobmsg_add_u32(&blob, "create_time",
				(uint32_t)sqlite3_column_int(stmt, 7));
		}
		sqlite3_finalize(stmt);
	}

	sqlite3_close(db);
	ubus_send_reply(ctx, req, blob.head);
	return 0;
}

/*============================================================
 * ubus object: ac-manager.logs
 *============================================================*/

static const struct blobmsg_policy logs_read_policy[] = {
	[0] = { .name = "limit", .type = BLOBMSG_TYPE_INT32 },
	[1] = { .name = "offset", .type = BLOBMSG_TYPE_INT32 },
};

static int
handle_logs_read(struct ubus_context *ctx, struct ubus_object *obj,
                  struct ubus_request_data *req, const char *method,
                  struct blob_attr *msg)
{
	struct blob_attr *tb[2];
	int limit = 100;
	int offset = 0;

	blobmsg_parse(logs_read_policy, ARRAY_SIZE(logs_read_policy),
	              tb, blobmsg_data(msg), blobmsg_data_len(msg));
	if (tb[0])
		limit = blobmsg_get_u32(tb[0]);
	if (tb[1])
		offset = blobmsg_get_u32(tb[1]);

	if (limit <= 0 || limit > MAX_LOG_ENTRIES)
		limit = MAX_LOG_ENTRIES;
	if (offset < 0)
		offset = 0;

	/* Read log file and parse JSON lines */
	FILE *f = fopen(LOG_FILE, "r");
	if (!f) {
		blob_buf_init(&blob, 0);
		void *arr = blobmsg_open_array(&blob, "logs");
		blobmsg_close_array(&blob, arr);
		ubus_send_reply(ctx, req, blob.head);
		return 0;
	}

	/* Seek to end, then read backwards */
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	long read_start = fsize - (long)(limit + offset) * 1024; /* estimate ~1KB per line */
	if (read_start < 0)
		read_start = 0;

	fseek(f, read_start, SEEK_SET);

	/* Skip partial first line if not at beginning */
	if (read_start > 0) {
		char c;
		while ((c = fgetc(f)) != '\n' && c != EOF)
			;
	}

	blob_buf_init(&blob, 0);
	void *arr = blobmsg_open_array(&blob, "logs");

	char line[1024];
	int count = 0;
	int skip = offset;

	while (count < limit && fgets(line, sizeof(line), f)) {
		/* Skip offset entries */
		if (skip > 0) {
			skip--;
			continue;
		}

		/* Parse structured JSON log line */
		/* Expected format: {"timestamp":"...","level":"...","module":"...","dev_sn":"...","msg":"..."} */
		/* Or simple format: 2026-01-01 12:00:00 INFO MAIN message */

		void *tbl = blobmsg_open_table(&blob, NULL);

		/* Try JSON parsing first */
		if (line[0] == '{') {
			/* Simple JSON line parsing */
			char timestamp[64] = {0};
			char level[16] = {0};
			char module[64] = {0};
			char msg[512] = {0};

			/* Extract fields using simple string search */
			char *p;
			p = strstr(line, "\"timestamp\"");
			if (p) {
				p = strchr(p + 11, '"');
				if (p) {
					p++;
					char *end = strchr(p, '"');
					if (end) {
						size_t len = end - p;
						if (len < sizeof(timestamp))
							memcpy(timestamp, p, len);
					}
				}
			}

			p = strstr(line, "\"level\"");
			if (p) {
				p = strchr(p + 7, '"');
				if (p) {
					p++;
					char *end = strchr(p, '"');
					if (end) {
						size_t len = end - p;
						if (len < sizeof(level))
							memcpy(level, p, len);
					}
				}
			}

			p = strstr(line, "\"module\"");
			if (p) {
				p = strchr(p + 8, '"');
				if (p) {
					p++;
					char *end = strchr(p, '"');
					if (end) {
						size_t len = end - p;
						if (len < sizeof(module))
							memcpy(module, p, len);
					}
				}
			}

			p = strstr(line, "\"msg\"");
			if (p) {
				p = strchr(p + 5, '"');
				if (p) {
					p++;
					char *end = strchr(p, '"');
					if (end) {
						size_t len = end - p;
						if (len < sizeof(msg))
							memcpy(msg, p, len);
					}
				}
			}

			blobmsg_add_string(&blob, "timestamp", timestamp);
			blobmsg_add_string(&blob, "level", level);
			blobmsg_add_string(&blob, "module", module);
			blobmsg_add_string(&blob, "msg", msg);
			blobmsg_add_string(&blob, "detail", line);
		} else {
			/* Non-JSON line - store as detail */
			line[strcspn(line, "\n")] = 0;
			blobmsg_add_string(&blob, "timestamp", "");
			blobmsg_add_string(&blob, "level", "");
			blobmsg_add_string(&blob, "module", "");
			blobmsg_add_string(&blob, "msg", "");
			blobmsg_add_string(&blob, "detail", line);
		}

		blobmsg_close_table(&blob, tbl);
		count++;
	}
	blobmsg_close_array(&blob, arr);

	fclose(f);
	ubus_send_reply(ctx, req, blob.head);
	return 0;
}

/*============================================================
 * ubus object definitions
 *============================================================*/

static const struct ubus_method status_methods[] = {
	UBUS_METHOD("get", handle_status_get, NULL),
};

static struct ubus_object_type status_object_type =
	UBUS_OBJECT_TYPE("ac-manager-status", status_methods);

static struct ubus_object status_object = {
	.name = "ac-manager.status",
	.type = &status_object_type,
	.methods = status_methods,
	.n_methods = ARRAY_SIZE(status_methods),
};

static const struct ubus_method devices_methods[] = {
	UBUS_METHOD("list", handle_devices_list, devices_list_policy),
	UBUS_METHOD("get", handle_device_get, device_get_policy),
};

static struct ubus_object_type devices_object_type =
	UBUS_OBJECT_TYPE("ac-manager-devices", devices_methods);

static struct ubus_object devices_object = {
	.name = "ac-manager.devices",
	.type = &devices_object_type,
	.methods = devices_methods,
	.n_methods = ARRAY_SIZE(devices_methods),
};

static const struct ubus_method templates_methods[] = {
	UBUS_METHOD("list", handle_templates_list, templates_list_policy),
	UBUS_METHOD("get", handle_template_get, template_get_policy),
};

static struct ubus_object_type templates_object_type =
	UBUS_OBJECT_TYPE("ac-manager-templates", templates_methods);

static struct ubus_object templates_object = {
	.name = "ac-manager.templates",
	.type = &templates_object_type,
	.methods = templates_methods,
	.n_methods = ARRAY_SIZE(templates_methods),
};

static const struct ubus_method logs_methods[] = {
	UBUS_METHOD("read", handle_logs_read, logs_read_policy),
};

static struct ubus_object_type logs_object_type =
	UBUS_OBJECT_TYPE("ac-manager-logs", logs_methods);

static struct ubus_object logs_object = {
	.name = "ac-manager.logs",
	.type = &logs_object_type,
	.methods = logs_methods,
	.n_methods = ARRAY_SIZE(logs_methods),
};

/*============================================================
 * Plugin init - register all ubus objects via rpcd plugin API
 *============================================================*/

static int
rpc_ac_manager_api_init(const struct rpc_daemon_ops *o, struct ubus_context *ctx)
{
	int ret;

	ret = ubus_add_object(ctx, &status_object);
	if (ret)
		return ret;

	ret = ubus_add_object(ctx, &devices_object);
	if (ret)
		return ret;

	ret = ubus_add_object(ctx, &templates_object);
	if (ret)
		return ret;

	ret = ubus_add_object(ctx, &logs_object);
	if (ret)
		return ret;

	return 0;
}

struct rpc_plugin rpc_plugin = {
	.init = rpc_ac_manager_api_init
};
