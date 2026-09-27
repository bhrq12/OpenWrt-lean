#include "db_interface.h"
#include "common/include/log.h"
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>

#ifdef USE_MYSQL
#include <mysql/mysql.h>
#endif

static int db_create_tables_sqlite(db_conn_t* conn);
static int db_create_tables_mysql(db_conn_t* conn);

int db_init(db_conn_t* conn, db_type_t type, const char* host, int port,
            const char* db_name, const char* user, const char* password) {
    if (!conn) {
        return -1;
    }
    
    memset(conn, 0, sizeof(db_conn_t));
    conn->type = type;
    
    if (type == DB_TYPE_SQLITE) {
        sqlite3* db;
        int rc = sqlite3_open(db_name, &db);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "SQLite open failed: %s", sqlite3_errmsg(db));
            sqlite3_close(db);
            return -1;
        }
        
        char* errmsg;
        rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, 0, &errmsg);
        if (rc != SQLITE_OK) {
            LOG_WARN("DB", "Failed to set WAL mode: %s", errmsg);
            sqlite3_free(errmsg);
        }
        
        conn->handle = db;
        db_create_tables(conn);
        
        LOG_INFO("DB", "SQLite initialized: %s", db_name);
    } else if (type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = mysql_init(NULL);
        if (!mysql) {
            LOG_ERROR("DB", "MySQL init failed");
            return -1;
        }
        
        if (!mysql_real_connect(mysql, host, user, password, db_name, port, NULL, 0)) {
            LOG_ERROR("DB", "MySQL connect failed: %s", mysql_error(mysql));
            mysql_close(mysql);
            return -1;
        }
        
        conn->handle = mysql;
        db_create_tables(conn);
        
        LOG_INFO("DB", "MySQL initialized: %s:%d/%s", host, port, db_name);
#else
        LOG_ERROR("DB", "MySQL support not compiled");
        return -1;
#endif
    } else {
        LOG_ERROR("DB", "Unknown DB type: %d", type);
        return -1;
    }
    
    return 0;
}

void db_close(db_conn_t* conn) {
    if (!conn || !conn->handle) {
        return;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3_close((sqlite3*)conn->handle);
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        mysql_close((MYSQL*)conn->handle);
#endif
    }
    
    conn->handle = NULL;
    LOG_INFO("DB", "Database connection closed");
}

int db_create_tables(db_conn_t* conn) {
    if (!conn || !conn->handle) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        return db_create_tables_sqlite(conn);
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        return db_create_tables_mysql(conn);
#else
        return -1;
#endif
    }
    
    return -1;
}

static int db_create_tables_sqlite(db_conn_t* conn) {
    sqlite3* db = (sqlite3*)conn->handle;
    char* errmsg;
    
    const char* create_devices = 
        "CREATE TABLE IF NOT EXISTS devices ("
        "dev_sn TEXT PRIMARY KEY,"
        "dev_model TEXT,"
        "fw_version TEXT,"
        "site_id TEXT,"
        "group_id TEXT,"
        "status INTEGER,"
        "cur_config_id TEXT,"
        "config_version INTEGER,"
        "register_time INTEGER,"
        "last_heartbeat INTEGER"
        ");";
    
    const char* create_config_templates =
        "CREATE TABLE IF NOT EXISTS config_templates ("
        "config_id TEXT PRIMARY KEY,"
        "template_name TEXT,"
        "template_type INTEGER,"
        "config_content TEXT,"
        "signature TEXT,"
        "version INTEGER,"
        "creator TEXT,"
        "create_time INTEGER"
        ");";
    
    const char* create_index1 = "CREATE INDEX IF NOT EXISTS idx_devices_site ON devices(site_id);";
    const char* create_index2 = "CREATE INDEX IF NOT EXISTS idx_devices_status ON devices(status);";
    
    int rc = sqlite3_exec(db, create_devices, NULL, 0, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DB", "Failed to create devices table: %s", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    
    rc = sqlite3_exec(db, create_config_templates, NULL, 0, &errmsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DB", "Failed to create config_templates table: %s", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    
    sqlite3_exec(db, create_index1, NULL, 0, &errmsg);
    sqlite3_exec(db, create_index2, NULL, 0, &errmsg);
    
    return 0;
}

static int db_create_tables_mysql(db_conn_t* conn) {
#ifdef USE_MYSQL
    MYSQL* mysql = (MYSQL*)conn->handle;
    
    const char* create_devices =
        "CREATE TABLE IF NOT EXISTS devices ("
        "dev_sn VARCHAR(32) PRIMARY KEY,"
        "dev_model VARCHAR(64),"
        "fw_version VARCHAR(32),"
        "site_id VARCHAR(32),"
        "group_id VARCHAR(32),"
        "status INT,"
        "cur_config_id VARCHAR(32),"
        "config_version INT,"
        "register_time BIGINT,"
        "last_heartbeat BIGINT,"
        "INDEX idx_devices_site (site_id),"
        "INDEX idx_devices_status (status)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
    
    const char* create_config_templates =
        "CREATE TABLE IF NOT EXISTS config_templates ("
        "config_id VARCHAR(32) PRIMARY KEY,"
        "template_name VARCHAR(64),"
        "template_type INT,"
        "config_content TEXT,"
        "signature VARCHAR(256),"
        "version INT,"
        "creator VARCHAR(64),"
        "create_time BIGINT"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
    
    if (mysql_query(mysql, create_devices) != 0) {
        LOG_ERROR("DB", "Failed to create devices table: %s", mysql_error(mysql));
        return -1;
    }
    
    if (mysql_query(mysql, create_config_templates) != 0) {
        LOG_ERROR("DB", "Failed to create config_templates table: %s", mysql_error(mysql));
        return -1;
    }
    
    return 0;
#else
    return -1;
#endif
}

int db_save_device(db_conn_t* conn, const db_device_t* device) {
    if (!conn || !conn->handle || !device) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "INSERT OR REPLACE INTO devices (dev_sn, dev_model, fw_version, "
                          "site_id, group_id, status, cur_config_id, config_version, "
                          "register_time, last_heartbeat) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, device->dev_sn, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, device->dev_model, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, device->fw_version, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, device->site_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, device->group_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 6, device->status);
        sqlite3_bind_text(stmt, 7, device->cur_config_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 8, device->config_version);
        sqlite3_bind_int64(stmt, 9, (sqlite3_int64)device->register_time);
        sqlite3_bind_int64(stmt, 10, (sqlite3_int64)device->last_heartbeat);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            LOG_ERROR("DB", "Failed to save device: %s", sqlite3_errmsg(db));
            return -1;
        }
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        
        const char* sql = "INSERT INTO devices (dev_sn, dev_model, fw_version, "
                          "site_id, group_id, status, cur_config_id, config_version, "
                          "register_time, last_heartbeat) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                          "ON DUPLICATE KEY UPDATE dev_model=VALUES(dev_model), "
                          "fw_version=VALUES(fw_version), site_id=VALUES(site_id), "
                          "group_id=VALUES(group_id), status=VALUES(status), "
                          "cur_config_id=VALUES(cur_config_id), config_version=VALUES(config_version), "
                          "last_heartbeat=VALUES(last_heartbeat)";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_BIND params[10];
        memset(params, 0, sizeof(params));
        
        params[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[0].buffer = (char*)device->dev_sn;
        params[0].buffer_length = strlen(device->dev_sn);
        
        params[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[1].buffer = (char*)device->dev_model;
        params[1].buffer_length = strlen(device->dev_model);
        
        params[2].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[2].buffer = (char*)device->fw_version;
        params[2].buffer_length = strlen(device->fw_version);
        
        params[3].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[3].buffer = (char*)device->site_id;
        params[3].buffer_length = strlen(device->site_id);
        
        params[4].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[4].buffer = (char*)device->group_id;
        params[4].buffer_length = strlen(device->group_id);
        
        params[5].buffer_type = MYSQL_TYPE_LONG;
        params[5].buffer = (char*)&device->status;
        
        params[6].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[6].buffer = (char*)device->cur_config_id;
        params[6].buffer_length = strlen(device->cur_config_id);
        
        params[7].buffer_type = MYSQL_TYPE_LONG;
        params[7].buffer = (char*)&device->config_version;
        
        params[8].buffer_type = MYSQL_TYPE_LONGLONG;
        params[8].buffer = (char*)&device->register_time;
        
        params[9].buffer_type = MYSQL_TYPE_LONGLONG;
        params[9].buffer = (char*)&device->last_heartbeat;
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to save device: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

int db_update_device_status(db_conn_t* conn, const char* dev_sn, device_status_t status) {
    if (!conn || !conn->handle || !dev_sn) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "UPDATE devices SET status=? WHERE dev_sn=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_int(stmt, 1, status);
        sqlite3_bind_text(stmt, 2, dev_sn, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            LOG_ERROR("DB", "Failed to update device status: %s", sqlite3_errmsg(db));
            return -1;
        }
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        
        const char* sql = "UPDATE devices SET status=? WHERE dev_sn=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_BIND params[2];
        memset(params, 0, sizeof(params));
        
        params[0].buffer_type = MYSQL_TYPE_LONG;
        params[0].buffer = (char*)&status;
        
        params[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[1].buffer = (char*)dev_sn;
        params[1].buffer_length = strlen(dev_sn);
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to update device status: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

int db_update_device_heartbeat(db_conn_t* conn, const char* dev_sn, time_t heartbeat) {
    if (!conn || !conn->handle || !dev_sn) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "UPDATE devices SET last_heartbeat=? WHERE dev_sn=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)heartbeat);
        sqlite3_bind_text(stmt, 2, dev_sn, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            LOG_ERROR("DB", "Failed to update device heartbeat: %s", sqlite3_errmsg(db));
            return -1;
        }
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        
        const char* sql = "UPDATE devices SET last_heartbeat=? WHERE dev_sn=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_BIND params[2];
        memset(params, 0, sizeof(params));
        
        params[0].buffer_type = MYSQL_TYPE_LONGLONG;
        params[0].buffer = (char*)&heartbeat;
        
        params[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[1].buffer = (char*)dev_sn;
        params[1].buffer_length = strlen(dev_sn);
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to update device heartbeat: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

int db_get_device(db_conn_t* conn, const char* dev_sn, db_device_t* device) {
    if (!conn || !conn->handle || !dev_sn || !device) {
        return -1;
    }
    
    memset(device, 0, sizeof(db_device_t));
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "SELECT * FROM devices WHERE dev_sn=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare query: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, dev_sn, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            strncpy(device->dev_sn, (const char*)sqlite3_column_text(stmt, 0), sizeof(device->dev_sn) - 1);
            strncpy(device->dev_model, (const char*)sqlite3_column_text(stmt, 1), sizeof(device->dev_model) - 1);
            strncpy(device->fw_version, (const char*)sqlite3_column_text(stmt, 2), sizeof(device->fw_version) - 1);
            strncpy(device->site_id, (const char*)sqlite3_column_text(stmt, 3), sizeof(device->site_id) - 1);
            strncpy(device->group_id, (const char*)sqlite3_column_text(stmt, 4), sizeof(device->group_id) - 1);
            device->status = (device_status_t)sqlite3_column_int(stmt, 5);
            strncpy(device->cur_config_id, (const char*)sqlite3_column_text(stmt, 6), sizeof(device->cur_config_id) - 1);
            device->config_version = sqlite3_column_int(stmt, 7);
            device->register_time = (time_t)sqlite3_column_int64(stmt, 8);
            device->last_heartbeat = (time_t)sqlite3_column_int64(stmt, 9);
            
            sqlite3_finalize(stmt);
            return 0;
        }
        
        sqlite3_finalize(stmt);
        LOG_DEBUG("DB", "Device not found: %s", dev_sn);
        return -1;
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        MYSQL_BIND params[1];
        MYSQL_BIND results[10];
        MYSQL_RES* meta;
        
        const char* sql = "SELECT * FROM devices WHERE dev_sn=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare query: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        memset(params, 0, sizeof(params));
        params[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[0].buffer = (char*)dev_sn;
        params[0].buffer_length = strlen(dev_sn);
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        memset(results, 0, sizeof(results));
        results[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[0].buffer = device->dev_sn;
        results[0].buffer_length = sizeof(device->dev_sn);
        
        results[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[1].buffer = device->dev_model;
        results[1].buffer_length = sizeof(device->dev_model);
        
        results[2].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[2].buffer = device->fw_version;
        results[2].buffer_length = sizeof(device->fw_version);
        
        results[3].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[3].buffer = device->site_id;
        results[3].buffer_length = sizeof(device->site_id);
        
        results[4].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[4].buffer = device->group_id;
        results[4].buffer_length = sizeof(device->group_id);
        
        results[5].buffer_type = MYSQL_TYPE_LONG;
        results[5].buffer = (char*)&device->status;
        
        results[6].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[6].buffer = device->cur_config_id;
        results[6].buffer_length = sizeof(device->cur_config_id);
        
        results[7].buffer_type = MYSQL_TYPE_LONG;
        results[7].buffer = (char*)&device->config_version;
        
        results[8].buffer_type = MYSQL_TYPE_LONGLONG;
        results[8].buffer = (char*)&device->register_time;
        
        results[9].buffer_type = MYSQL_TYPE_LONGLONG;
        results[9].buffer = (char*)&device->last_heartbeat;
        
        if (mysql_stmt_bind_result(stmt, results) != 0) {
            LOG_ERROR("DB", "Failed to bind results: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to query device: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_store_result(stmt) != 0) {
            LOG_ERROR("DB", "Failed to store result: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_fetch(stmt) == 0) {
            mysql_stmt_close(stmt);
            return 0;
        }
        
        mysql_stmt_close(stmt);
        LOG_DEBUG("DB", "Device not found: %s", dev_sn);
        return -1;
#endif
    }
    
    return -1;
}

int db_delete_device(db_conn_t* conn, const char* dev_sn) {
    if (!conn || !conn->handle || !dev_sn) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "DELETE FROM devices WHERE dev_sn=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, dev_sn, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            LOG_ERROR("DB", "Failed to delete device: %s", sqlite3_errmsg(db));
            return -1;
        }
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        
        const char* sql = "DELETE FROM devices WHERE dev_sn=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_BIND params[1];
        memset(params, 0, sizeof(params));
        
        params[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[0].buffer = (char*)dev_sn;
        params[0].buffer_length = strlen(dev_sn);
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to delete device: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

int db_save_config_template(db_conn_t* conn, const db_config_template_t* template) {
    if (!conn || !conn->handle || !template) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "INSERT OR REPLACE INTO config_templates (config_id, template_name, "
                          "template_type, config_content, signature, version, creator, create_time) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, template->config_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, template->template_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, template->template_type);
        sqlite3_bind_text(stmt, 4, template->config_content ? template->config_content : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, template->signature, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 6, template->version);
        sqlite3_bind_text(stmt, 7, template->creator, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 8, (sqlite3_int64)template->create_time);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            LOG_ERROR("DB", "Failed to save config template: %s", sqlite3_errmsg(db));
            return -1;
        }
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        
        const char* sql = "INSERT INTO config_templates (config_id, template_name, "
                          "template_type, config_content, signature, version, creator, create_time) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                          "ON DUPLICATE KEY UPDATE template_name=VALUES(template_name), "
                          "template_type=VALUES(template_type), config_content=VALUES(config_content), "
                          "signature=VALUES(signature), version=VALUES(version), "
                          "creator=VALUES(creator), create_time=VALUES(create_time)";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_BIND params[8];
        memset(params, 0, sizeof(params));
        
        params[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[0].buffer = (char*)template->config_id;
        params[0].buffer_length = strlen(template->config_id);
        
        params[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[1].buffer = (char*)template->template_name;
        params[1].buffer_length = strlen(template->template_name);
        
        params[2].buffer_type = MYSQL_TYPE_LONG;
        params[2].buffer = (char*)&template->template_type;
        
        params[3].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[3].buffer = (char*)(template->config_content ? template->config_content : "");
        params[3].buffer_length = template->config_content ? strlen(template->config_content) : 0;
        
        params[4].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[4].buffer = (char*)template->signature;
        params[4].buffer_length = strlen(template->signature);
        
        params[5].buffer_type = MYSQL_TYPE_LONG;
        params[5].buffer = (char*)&template->version;
        
        params[6].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[6].buffer = (char*)template->creator;
        params[6].buffer_length = strlen(template->creator);
        
        params[7].buffer_type = MYSQL_TYPE_LONGLONG;
        params[7].buffer = (char*)&template->create_time;
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to save config template: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

int db_get_config_template(db_conn_t* conn, const char* config_id, db_config_template_t* template) {
    if (!conn || !conn->handle || !config_id || !template) {
        return -1;
    }
    
    memset(template, 0, sizeof(db_config_template_t));
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "SELECT * FROM config_templates WHERE config_id=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare query: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, config_id, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            strncpy(template->config_id, (const char*)sqlite3_column_text(stmt, 0), sizeof(template->config_id) - 1);
            strncpy(template->template_name, (const char*)sqlite3_column_text(stmt, 1), sizeof(template->template_name) - 1);
            template->template_type = sqlite3_column_int(stmt, 2);
            
            const char* content = (const char*)sqlite3_column_text(stmt, 3);
            if (content) {
                template->config_content = strdup(content);
            }
            
            strncpy(template->signature, (const char*)sqlite3_column_text(stmt, 4), sizeof(template->signature) - 1);
            template->version = sqlite3_column_int(stmt, 5);
            strncpy(template->creator, (const char*)sqlite3_column_text(stmt, 6), sizeof(template->creator) - 1);
            template->create_time = (time_t)sqlite3_column_int64(stmt, 7);
            
            sqlite3_finalize(stmt);
            return 0;
        }
        
        sqlite3_finalize(stmt);
        LOG_DEBUG("DB", "Config template not found: %s", config_id);
        return -1;
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        MYSQL_BIND params[1];
        
        const char* sql = "SELECT * FROM config_templates WHERE config_id=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare query: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        memset(params, 0, sizeof(params));
        params[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[0].buffer = (char*)config_id;
        params[0].buffer_length = strlen(config_id);
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to query config template: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_RES* result = mysql_stmt_result_metadata(stmt);
        if (!result) {
            LOG_ERROR("DB", "Failed to get result metadata: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_ROW row;
        MYSQL_FIELD* fields = mysql_fetch_fields(result);
        int num_fields = mysql_num_fields(result);
        
        mysql_free_result(result);
        
        if (mysql_stmt_store_result(stmt) != 0) {
            LOG_ERROR("DB", "Failed to store result: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_fetch(stmt) == 0) {
            char** row_data = (char**)malloc(num_fields * sizeof(char*));
            unsigned long* lengths = (unsigned long*)malloc(num_fields * sizeof(unsigned long));
            
            if (mysql_stmt_fetch_row(stmt, row_data, lengths) == 0) {
                strncpy(template->config_id, row_data[0] ? row_data[0] : "", sizeof(template->config_id) - 1);
                strncpy(template->template_name, row_data[1] ? row_data[1] : "", sizeof(template->template_name) - 1);
                template->template_type = row_data[2] ? atoi(row_data[2]) : 0;
                
                if (row_data[3]) {
                    template->config_content = strdup(row_data[3]);
                }
                
                strncpy(template->signature, row_data[4] ? row_data[4] : "", sizeof(template->signature) - 1);
                template->version = row_data[5] ? atoi(row_data[5]) : 0;
                strncpy(template->creator, row_data[6] ? row_data[6] : "", sizeof(template->creator) - 1);
                template->create_time = row_data[7] ? (time_t)atoll(row_data[7]) : 0;
            }
            
            free(row_data);
            free(lengths);
            
            mysql_stmt_close(stmt);
            return 0;
        }
        
        mysql_stmt_close(stmt);
        LOG_DEBUG("DB", "Config template not found: %s", config_id);
        return -1;
#endif
    }
    
    return -1;
}

int db_delete_config_template(db_conn_t* conn, const char* config_id) {
    if (!conn || !conn->handle || !config_id) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "DELETE FROM config_templates WHERE config_id=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, config_id, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) {
            LOG_ERROR("DB", "Failed to delete config template: %s", sqlite3_errmsg(db));
            return -1;
        }
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        
        const char* sql = "DELETE FROM config_templates WHERE config_id=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare statement: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_BIND params[1];
        memset(params, 0, sizeof(params));
        
        params[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[0].buffer = (char*)config_id;
        params[0].buffer_length = strlen(config_id);
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to delete config template: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

int db_get_devices_by_site(db_conn_t* conn, const char* site_id, db_device_t** devices, int* count) {
    *devices = NULL;
    *count = 0;
    
    if (!conn || !conn->handle || !site_id) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "SELECT * FROM devices WHERE site_id=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare query: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, site_id, -1, SQLITE_STATIC);
        
        int total = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            total++;
        }
        
        if (total > 0) {
            *devices = (db_device_t*)malloc(total * sizeof(db_device_t));
            if (!*devices) {
                sqlite3_finalize(stmt);
                return -1;
            }
            
            sqlite3_reset(stmt);
            int idx = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                memset(&(*devices)[idx], 0, sizeof(db_device_t));
                strncpy((*devices)[idx].dev_sn, (const char*)sqlite3_column_text(stmt, 0), sizeof((*devices)[idx].dev_sn) - 1);
                strncpy((*devices)[idx].dev_model, (const char*)sqlite3_column_text(stmt, 1), sizeof((*devices)[idx].dev_model) - 1);
                strncpy((*devices)[idx].fw_version, (const char*)sqlite3_column_text(stmt, 2), sizeof((*devices)[idx].fw_version) - 1);
                strncpy((*devices)[idx].site_id, (const char*)sqlite3_column_text(stmt, 3), sizeof((*devices)[idx].site_id) - 1);
                strncpy((*devices)[idx].group_id, (const char*)sqlite3_column_text(stmt, 4), sizeof((*devices)[idx].group_id) - 1);
                (*devices)[idx].status = (device_status_t)sqlite3_column_int(stmt, 5);
                (*devices)[idx].last_heartbeat = (time_t)sqlite3_column_int64(stmt, 9);
                idx++;
            }
            *count = total;
        }
        
        sqlite3_finalize(stmt);
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        MYSQL_BIND params[1];
        
        const char* sql = "SELECT * FROM devices WHERE site_id=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare query: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        memset(params, 0, sizeof(params));
        params[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        params[0].buffer = (char*)site_id;
        params[0].buffer_length = strlen(site_id);
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to query devices: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_store_result(stmt) != 0) {
            LOG_ERROR("DB", "Failed to store result: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
        if (!meta) {
            LOG_ERROR("DB", "Failed to get result metadata: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        int num_fields = mysql_num_fields(meta);
        mysql_free_result(meta);
        
        MYSQL_BIND results[10];
        memset(results, 0, sizeof(results));
        
        db_device_t temp_device;
        memset(&temp_device, 0, sizeof(temp_device));
        
        results[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[0].buffer = temp_device.dev_sn;
        results[0].buffer_length = sizeof(temp_device.dev_sn);
        
        results[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[1].buffer = temp_device.dev_model;
        results[1].buffer_length = sizeof(temp_device.dev_model);
        
        results[2].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[2].buffer = temp_device.fw_version;
        results[2].buffer_length = sizeof(temp_device.fw_version);
        
        results[3].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[3].buffer = temp_device.site_id;
        results[3].buffer_length = sizeof(temp_device.site_id);
        
        results[4].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[4].buffer = temp_device.group_id;
        results[4].buffer_length = sizeof(temp_device.group_id);
        
        results[5].buffer_type = MYSQL_TYPE_LONG;
        results[5].buffer = (char*)&temp_device.status;
        
        results[6].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[6].buffer = temp_device.cur_config_id;
        results[6].buffer_length = sizeof(temp_device.cur_config_id);
        
        results[7].buffer_type = MYSQL_TYPE_LONG;
        results[7].buffer = (char*)&temp_device.config_version;
        
        results[8].buffer_type = MYSQL_TYPE_LONGLONG;
        results[8].buffer = (char*)&temp_device.register_time;
        
        results[9].buffer_type = MYSQL_TYPE_LONGLONG;
        results[9].buffer = (char*)&temp_device.last_heartbeat;
        
        if (mysql_stmt_bind_result(stmt, results) != 0) {
            LOG_ERROR("DB", "Failed to bind results: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        int total = mysql_stmt_num_rows(stmt);
        if (total > 0) {
            *devices = (db_device_t*)malloc(total * sizeof(db_device_t));
            if (!*devices) {
                mysql_stmt_close(stmt);
                return -1;
            }
            
            int idx = 0;
            while (mysql_stmt_fetch(stmt) == 0) {
                memset(&(*devices)[idx], 0, sizeof(db_device_t));
                memcpy(&(*devices)[idx], &temp_device, sizeof(db_device_t));
                idx++;
            }
            *count = total;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

void db_free_devices(db_device_t* devices, int count) {
    if (!devices) {
        return;
    }
    free(devices);
}

int db_get_devices_by_status(db_conn_t* conn, device_status_t status, db_device_t** devices, int* count) {
    *devices = NULL;
    *count = 0;
    
    if (!conn || !conn->handle) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "SELECT * FROM devices WHERE status=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare query: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_int(stmt, 1, status);
        
        int total = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            total++;
        }
        
        if (total > 0) {
            *devices = (db_device_t*)malloc(total * sizeof(db_device_t));
            if (!*devices) {
                sqlite3_finalize(stmt);
                return -1;
            }
            
            sqlite3_reset(stmt);
            int idx = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                memset(&(*devices)[idx], 0, sizeof(db_device_t));
                strncpy((*devices)[idx].dev_sn, (const char*)sqlite3_column_text(stmt, 0), sizeof((*devices)[idx].dev_sn) - 1);
                strncpy((*devices)[idx].dev_model, (const char*)sqlite3_column_text(stmt, 1), sizeof((*devices)[idx].dev_model) - 1);
                strncpy((*devices)[idx].fw_version, (const char*)sqlite3_column_text(stmt, 2), sizeof((*devices)[idx].fw_version) - 1);
                strncpy((*devices)[idx].site_id, (const char*)sqlite3_column_text(stmt, 3), sizeof((*devices)[idx].site_id) - 1);
                strncpy((*devices)[idx].group_id, (const char*)sqlite3_column_text(stmt, 4), sizeof((*devices)[idx].group_id) - 1);
                (*devices)[idx].status = (device_status_t)sqlite3_column_int(stmt, 5);
                (*devices)[idx].last_heartbeat = (time_t)sqlite3_column_int64(stmt, 9);
                idx++;
            }
            *count = total;
        }
        
        sqlite3_finalize(stmt);
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        MYSQL_BIND params[1];
        
        const char* sql = "SELECT * FROM devices WHERE status=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare query: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        memset(params, 0, sizeof(params));
        params[0].buffer_type = MYSQL_TYPE_LONG;
        params[0].buffer = (char*)&status;
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to query devices: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_store_result(stmt) != 0) {
            LOG_ERROR("DB", "Failed to store result: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
        if (!meta) {
            LOG_ERROR("DB", "Failed to get result metadata: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_free_result(meta);
        
        MYSQL_BIND results[10];
        memset(results, 0, sizeof(results));
        
        db_device_t temp_device;
        memset(&temp_device, 0, sizeof(temp_device));
        
        results[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[0].buffer = temp_device.dev_sn;
        results[0].buffer_length = sizeof(temp_device.dev_sn);
        
        results[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[1].buffer = temp_device.dev_model;
        results[1].buffer_length = sizeof(temp_device.dev_model);
        
        results[2].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[2].buffer = temp_device.fw_version;
        results[2].buffer_length = sizeof(temp_device.fw_version);
        
        results[3].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[3].buffer = temp_device.site_id;
        results[3].buffer_length = sizeof(temp_device.site_id);
        
        results[4].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[4].buffer = temp_device.group_id;
        results[4].buffer_length = sizeof(temp_device.group_id);
        
        results[5].buffer_type = MYSQL_TYPE_LONG;
        results[5].buffer = (char*)&temp_device.status;
        
        results[6].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[6].buffer = temp_device.cur_config_id;
        results[6].buffer_length = sizeof(temp_device.cur_config_id);
        
        results[7].buffer_type = MYSQL_TYPE_LONG;
        results[7].buffer = (char*)&temp_device.config_version;
        
        results[8].buffer_type = MYSQL_TYPE_LONGLONG;
        results[8].buffer = (char*)&temp_device.register_time;
        
        results[9].buffer_type = MYSQL_TYPE_LONGLONG;
        results[9].buffer = (char*)&temp_device.last_heartbeat;
        
        if (mysql_stmt_bind_result(stmt, results) != 0) {
            LOG_ERROR("DB", "Failed to bind results: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        int total = mysql_stmt_num_rows(stmt);
        if (total > 0) {
            *devices = (db_device_t*)malloc(total * sizeof(db_device_t));
            if (!*devices) {
                mysql_stmt_close(stmt);
                return -1;
            }
            
            int idx = 0;
            while (mysql_stmt_fetch(stmt) == 0) {
                memset(&(*devices)[idx], 0, sizeof(db_device_t));
                memcpy(&(*devices)[idx], &temp_device, sizeof(db_device_t));
                idx++;
            }
            *count = total;
        }
        
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

int db_get_config_templates(db_conn_t* conn, db_config_template_t** templates, int* count) {
    *templates = NULL;
    *count = 0;
    
    if (!conn || !conn->handle) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "SELECT * FROM config_templates";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare query: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        int total = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            total++;
        }
        
        if (total > 0) {
            *templates = (db_config_template_t*)malloc(total * sizeof(db_config_template_t));
            if (!*templates) {
                sqlite3_finalize(stmt);
                return -1;
            }
            
            sqlite3_reset(stmt);
            int idx = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                memset(&(*templates)[idx], 0, sizeof(db_config_template_t));
                strncpy((*templates)[idx].config_id, (const char*)sqlite3_column_text(stmt, 0), sizeof((*templates)[idx].config_id) - 1);
                strncpy((*templates)[idx].template_name, (const char*)sqlite3_column_text(stmt, 1), sizeof((*templates)[idx].template_name) - 1);
                (*templates)[idx].template_type = sqlite3_column_int(stmt, 2);
                (*templates)[idx].version = sqlite3_column_int(stmt, 5);
                idx++;
            }
            *count = total;
        }
        
        sqlite3_finalize(stmt);
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        
        const char* sql = "SELECT * FROM config_templates";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare query: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to query config templates: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_store_result(stmt) != 0) {
            LOG_ERROR("DB", "Failed to store result: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
        if (!meta) {
            LOG_ERROR("DB", "Failed to get result metadata: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_free_result(meta);
        
        MYSQL_BIND results[8];
        memset(results, 0, sizeof(results));
        
        db_config_template_t temp_template;
        memset(&temp_template, 0, sizeof(temp_template));
        
        results[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[0].buffer = temp_template.config_id;
        results[0].buffer_length = sizeof(temp_template.config_id);
        
        results[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[1].buffer = temp_template.template_name;
        results[1].buffer_length = sizeof(temp_template.template_name);
        
        results[2].buffer_type = MYSQL_TYPE_LONG;
        results[2].buffer = (char*)&temp_template.template_type;
        
        results[3].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[3].buffer = (char*)malloc(8192);
        results[3].buffer_length = 8192;
        
        results[4].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[4].buffer = temp_template.signature;
        results[4].buffer_length = sizeof(temp_template.signature);
        
        results[5].buffer_type = MYSQL_TYPE_LONG;
        results[5].buffer = (char*)&temp_template.version;
        
        results[6].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[6].buffer = temp_template.creator;
        results[6].buffer_length = sizeof(temp_template.creator);
        
        results[7].buffer_type = MYSQL_TYPE_LONGLONG;
        results[7].buffer = (char*)&temp_template.create_time;
        
        if (mysql_stmt_bind_result(stmt, results) != 0) {
            LOG_ERROR("DB", "Failed to bind results: %s", mysql_stmt_error(stmt));
            free(results[3].buffer);
            mysql_stmt_close(stmt);
            return -1;
        }
        
        int total = mysql_stmt_num_rows(stmt);
        if (total > 0) {
            *templates = (db_config_template_t*)malloc(total * sizeof(db_config_template_t));
            if (!*templates) {
                free(results[3].buffer);
                mysql_stmt_close(stmt);
                return -1;
            }
            
            int idx = 0;
            while (mysql_stmt_fetch(stmt) == 0) {
                memset(&(*templates)[idx], 0, sizeof(db_config_template_t));
                strncpy((*templates)[idx].config_id, temp_template.config_id, sizeof((*templates)[idx].config_id) - 1);
                strncpy((*templates)[idx].template_name, temp_template.template_name, sizeof((*templates)[idx].template_name) - 1);
                (*templates)[idx].template_type = temp_template.template_type;
                if (results[3].buffer) {
                    (*templates)[idx].config_content = strdup((char*)results[3].buffer);
                }
                strncpy((*templates)[idx].signature, temp_template.signature, sizeof((*templates)[idx].signature) - 1);
                (*templates)[idx].version = temp_template.version;
                strncpy((*templates)[idx].creator, temp_template.creator, sizeof((*templates)[idx].creator) - 1);
                (*templates)[idx].create_time = temp_template.create_time;
                idx++;
            }
            *count = total;
        }
        
        free(results[3].buffer);
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}

void db_free_config_templates(db_config_template_t* templates, int count) {
    if (!templates) {
        return;
    }
    for (int i = 0; i < count; i++) {
        if (templates[i].config_content) {
            free(templates[i].config_content);
        }
    }
    free(templates);
}

int db_get_config_templates_by_type(db_conn_t* conn, int template_type,
                                    db_config_template_t** templates, int* count) {
    *templates = NULL;
    *count = 0;
    
    if (!conn || !conn->handle) {
        return -1;
    }
    
    if (conn->type == DB_TYPE_SQLITE) {
        sqlite3* db = (sqlite3*)conn->handle;
        sqlite3_stmt* stmt;
        
        const char* sql = "SELECT * FROM config_templates WHERE template_type=?";
        
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DB", "Failed to prepare query: %s", sqlite3_errmsg(db));
            return -1;
        }
        
        sqlite3_bind_int(stmt, 1, template_type);
        
        int total = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            total++;
        }
        
        if (total > 0) {
            *templates = (db_config_template_t*)malloc(total * sizeof(db_config_template_t));
            if (!*templates) {
                sqlite3_finalize(stmt);
                return -1;
            }
            
            sqlite3_reset(stmt);
            int idx = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                memset(&(*templates)[idx], 0, sizeof(db_config_template_t));
                strncpy((*templates)[idx].config_id, (const char*)sqlite3_column_text(stmt, 0), sizeof((*templates)[idx].config_id) - 1);
                strncpy((*templates)[idx].template_name, (const char*)sqlite3_column_text(stmt, 1), sizeof((*templates)[idx].template_name) - 1);
                (*templates)[idx].template_type = sqlite3_column_int(stmt, 2);
                (*templates)[idx].version = sqlite3_column_int(stmt, 5);
                idx++;
            }
            *count = total;
        }
        
        sqlite3_finalize(stmt);
    } else if (conn->type == DB_TYPE_MYSQL) {
#ifdef USE_MYSQL
        MYSQL* mysql = (MYSQL*)conn->handle;
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        MYSQL_BIND params[1];
        
        const char* sql = "SELECT * FROM config_templates WHERE template_type=?";
        
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
            LOG_ERROR("DB", "Failed to prepare query: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        memset(params, 0, sizeof(params));
        params[0].buffer_type = MYSQL_TYPE_LONG;
        params[0].buffer = (char*)&template_type;
        
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            LOG_ERROR("DB", "Failed to bind params: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("DB", "Failed to query config templates: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        if (mysql_stmt_store_result(stmt) != 0) {
            LOG_ERROR("DB", "Failed to store result: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
        if (!meta) {
            LOG_ERROR("DB", "Failed to get result metadata: %s", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return -1;
        }
        
        mysql_free_result(meta);
        
        MYSQL_BIND results[8];
        memset(results, 0, sizeof(results));
        
        db_config_template_t temp_template;
        memset(&temp_template, 0, sizeof(temp_template));
        
        results[0].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[0].buffer = temp_template.config_id;
        results[0].buffer_length = sizeof(temp_template.config_id);
        
        results[1].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[1].buffer = temp_template.template_name;
        results[1].buffer_length = sizeof(temp_template.template_name);
        
        results[2].buffer_type = MYSQL_TYPE_LONG;
        results[2].buffer = (char*)&temp_template.template_type;
        
        results[3].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[3].buffer = (char*)malloc(8192);
        results[3].buffer_length = 8192;
        
        results[4].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[4].buffer = temp_template.signature;
        results[4].buffer_length = sizeof(temp_template.signature);
        
        results[5].buffer_type = MYSQL_TYPE_LONG;
        results[5].buffer = (char*)&temp_template.version;
        
        results[6].buffer_type = MYSQL_TYPE_VAR_STRING;
        results[6].buffer = temp_template.creator;
        results[6].buffer_length = sizeof(temp_template.creator);
        
        results[7].buffer_type = MYSQL_TYPE_LONGLONG;
        results[7].buffer = (char*)&temp_template.create_time;
        
        if (mysql_stmt_bind_result(stmt, results) != 0) {
            LOG_ERROR("DB", "Failed to bind results: %s", mysql_stmt_error(stmt));
            free(results[3].buffer);
            mysql_stmt_close(stmt);
            return -1;
        }
        
        int total = mysql_stmt_num_rows(stmt);
        if (total > 0) {
            *templates = (db_config_template_t*)malloc(total * sizeof(db_config_template_t));
            if (!*templates) {
                free(results[3].buffer);
                mysql_stmt_close(stmt);
                return -1;
            }
            
            int idx = 0;
            while (mysql_stmt_fetch(stmt) == 0) {
                memset(&(*templates)[idx], 0, sizeof(db_config_template_t));
                strncpy((*templates)[idx].config_id, temp_template.config_id, sizeof((*templates)[idx].config_id) - 1);
                strncpy((*templates)[idx].template_name, temp_template.template_name, sizeof((*templates)[idx].template_name) - 1);
                (*templates)[idx].template_type = temp_template.template_type;
                if (results[3].buffer) {
                    (*templates)[idx].config_content = strdup((char*)results[3].buffer);
                }
                strncpy((*templates)[idx].signature, temp_template.signature, sizeof((*templates)[idx].signature) - 1);
                (*templates)[idx].version = temp_template.version;
                strncpy((*templates)[idx].creator, temp_template.creator, sizeof((*templates)[idx].creator) - 1);
                (*templates)[idx].create_time = temp_template.create_time;
                idx++;
            }
            *count = total;
        }
        
        free(results[3].buffer);
        mysql_stmt_close(stmt);
#endif
    }
    
    return 0;
}