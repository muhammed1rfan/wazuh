/**
 * @file fim_db_files.c
 * @brief Definition of FIM database for files library.
 * @date 2020-09-9
 *
 * @copyright Copyright (C) 2015-2021 Wazuh, Inc.
 */

#include "fim_db_files.h"

#ifdef WAZUH_UNIT_TESTING
/* Remove static qualifier when unit testing */
#define static

/* Replace assert with mock_assert */
extern void mock_assert(const int result, const char *const expression, const char *const file, const int line);
#undef assert
#define assert(expression) mock_assert((int)(expression), #expression, __FILE__, __LINE__);
#endif

extern const char *SQL_STMT[];

// Convenience macros
#define fim_db_bind_set_scanned(fim_sql, path) fim_db_bind_path(fim_sql, FIMDB_STMT_SET_SCANNED, path)

#define fim_db_bind_get_path_from_pattern(fim_sql, path) \
    fim_db_bind_path(fim_sql, FIMDB_STMT_GET_PATH_FROM_PATTERN, path)

// bindings


/**
 * @brief Binds a range of paths.
 *
 * @param fim_sql FIM database structure.
 * @param file_path File name of the file to insert.
 * @param entry FIM entry data structure.
 */
static void fim_db_bind_replace_entry(fdb_t *fim_sql, const char *file_path, const fim_file_data *entry);


/**
 * @brief Binds a path into a statement.
 *
 * @param fim_sql FIM database structure.
 * @param index Index of the particular statement.
 * @param file_path File name of the file to insert.
 */
static void fim_db_bind_path(fdb_t *fim_sql, int index,
                             const char * file_path);


/**
 * @brief Binds data into a get inode statement.
 *
 * @param fim_sql FIM database structure.
 * @param index Index of the particular statement.
 * @param inode Inode of the file.
 * @param dev dev of the file.
 */
static void fim_db_bind_get_inode(fdb_t *fim_sql, int index,
                                  unsigned long int inode,
                                  unsigned long int dev);


/**
 * @brief Removes paths from the FIM DB if its configuration matches with the one provided
 *
 * @param fim_sql FIM database structure.
 * @param entry Entry data to be removed.
 * @param mutex FIM database's mutex for thread synchronization.
 * @param fim_ev_mode FIM Mode (scheduled/realtime/whodata)
 * @param configuration Position of the configuration that triggered the deletion of entries.
 * @param _unused_parameter Needed for this function to be a valid FIM DB callback.
 */
void fim_db_remove_validated_path(fdb_t *fim_sql,
                                  fim_entry *entry,
                                  pthread_mutex_t *mutex,
                                  void *evt_data,
                                  void *configuration,
                                  void *_unused_patameter);

int fim_db_get_not_scanned(fdb_t * fim_sql, fim_tmp_file **file, int storage) {
    if ((*file = fim_db_create_temp_file(storage)) == NULL) {
        return FIMDB_ERR;
    }

    int ret = fim_db_process_get_query(fim_sql, FIM_TYPE_FILE, FIMDB_STMT_GET_NOT_SCANNED,
                                       fim_db_callback_save_path, storage, (void*) *file);

    if (*file && (*file)->elements == 0) {
        fim_db_clean_file(file, storage);
    }

    return ret;

}

// LCOV_EXCL_START
int fim_db_delete_not_scanned(fdb_t * fim_sql, fim_tmp_file *file, pthread_mutex_t *mutex, int storage) {
    event_data_t evt_data = { .mode = FIM_SCHEDULED, .w_evt = NULL, .report_event = TRUE, .type = FIM_DELETE };
    return fim_db_process_read_file(fim_sql, file, FIM_TYPE_FILE, mutex, fim_delete_file_event, storage,
                                    (void *)&evt_data, NULL, NULL);
}

int fim_db_delete_range(fdb_t *fim_sql,
                        fim_tmp_file *file,
                        pthread_mutex_t *mutex,
                        int storage,
                        event_data_t *evt_data,
                        directory_t *configuration) {
    return fim_db_process_read_file(fim_sql, file, FIM_TYPE_FILE, mutex, fim_db_remove_validated_path, storage,
                                    evt_data, configuration, NULL);
}

int fim_db_process_missing_entry(fdb_t *fim_sql,
                                 fim_tmp_file *file,
                                 pthread_mutex_t *mutex,
                                 int storage,
                                 event_data_t *evt_data) {
    return fim_db_process_read_file(fim_sql, file, FIM_TYPE_FILE, mutex, fim_delete_file_event, storage, evt_data, NULL,
                                    NULL);
}

int fim_db_remove_wildcard_entry(fdb_t *fim_sql,
                                 fim_tmp_file *file,
                                 pthread_mutex_t *mutex,
                                 int storage,
                                 event_data_t *evt_data,
                                 directory_t *configuration) {
    return fim_db_process_read_file(fim_sql, file, FIM_TYPE_FILE, mutex, fim_generate_delete_event, storage, evt_data,
                                    configuration, NULL);
}
// LCOV_EXCL_STOP

fim_entry *fim_db_decode_full_row(sqlite3_stmt *stmt) {

    fim_entry *entry = NULL;

    os_calloc(1, sizeof(fim_entry), entry);
    entry->type = FIM_TYPE_FILE;
    os_strdup((char *)sqlite3_column_text(stmt, 0), entry->file_entry.path);

    os_calloc(1, sizeof(fim_file_data), entry->file_entry.data);
    entry->file_entry.data->mode = (unsigned int)sqlite3_column_int(stmt, 2);
    entry->file_entry.data->last_event = (time_t)sqlite3_column_int(stmt, 3);
    entry->file_entry.data->scanned = (time_t)sqlite3_column_int(stmt, 4);
    entry->file_entry.data->options = (time_t)sqlite3_column_int(stmt, 5);
    strncpy(entry->file_entry.data->checksum, (char *)sqlite3_column_text(stmt, 6), sizeof(os_sha1) - 1);
    entry->file_entry.data->dev = (unsigned long int)sqlite3_column_int(stmt, 7);
    entry->file_entry.data->inode = (unsigned long int)sqlite3_column_int64(stmt, 8);
    entry->file_entry.data->size = (unsigned int)sqlite3_column_int(stmt, 9);
    sqlite_strdup((char *)sqlite3_column_text(stmt, 10), entry->file_entry.data->perm);
    sqlite_strdup((char *)sqlite3_column_text(stmt, 11), entry->file_entry.data->attributes);
    sqlite_strdup((char *)sqlite3_column_text(stmt, 12), entry->file_entry.data->uid);
    sqlite_strdup((char *)sqlite3_column_text(stmt, 13), entry->file_entry.data->gid);
    sqlite_strdup((char *)sqlite3_column_text(stmt, 14), entry->file_entry.data->user_name);
    sqlite_strdup((char *)sqlite3_column_text(stmt, 15), entry->file_entry.data->group_name);
    strncpy(entry->file_entry.data->hash_md5, (char *)sqlite3_column_text(stmt, 16), sizeof(os_md5) - 1);
    strncpy(entry->file_entry.data->hash_sha1, (char *)sqlite3_column_text(stmt, 17), sizeof(os_sha1) - 1);
    strncpy(entry->file_entry.data->hash_sha256, (char *)sqlite3_column_text(stmt, 18), sizeof(os_sha256) - 1);
    entry->file_entry.data->mtime = (unsigned int)sqlite3_column_int(stmt, 19);

    return entry;
}

/* No needed bind FIMDB_STMT_GET_LAST_ROWID, FIMDB_STMT_GET_NOT_SCANNED,
   FIMDB_STMT_SET_ALL_UNSCANNED, FIMDB_STMT_DELETE_UNSCANNED */

/* FIMDB_STMT_REPLACE_ENTRY */
void fim_db_bind_replace_entry(fdb_t *fim_sql, const char *file_path, const fim_file_data *entry) {
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 1, file_path, -1, NULL);
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 2, entry->mode);
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 3, entry->last_event);
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 4, entry->scanned);
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 5, entry->options);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 6, entry->checksum, -1, NULL);
#ifndef WIN32
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 7, entry->dev);
    sqlite3_bind_int64(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 8, entry->inode);
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 9, entry->size);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 10, entry->perm, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 11, entry->attributes, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 12, entry->uid, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 13, entry->gid, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 14, entry->user_name, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 15, entry->group_name, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 16, entry->hash_md5, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 17, entry->hash_sha1, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 18, entry->hash_sha256, -1, NULL);
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 19, entry->mtime);
#else
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 7, entry->size);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 8, entry->perm, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 9, entry->attributes, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 10, entry->uid, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 11, entry->gid, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 12, entry->user_name, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 13, entry->group_name, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 14, entry->hash_md5, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 15, entry->hash_sha1, -1, NULL);
    sqlite3_bind_text(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 16, entry->hash_sha256, -1, NULL);
    sqlite3_bind_int(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY], 17, entry->mtime);
#endif
}

/* FIMDB_STMT_GET_PATH
 * FIMDB_STMT_DELETE_PATH
 * FIMDB_STMT_SET_SCANNED
 * FIMDB_STMT_GET_PATH_FROM_PATTERN */
void fim_db_bind_path(fdb_t *fim_sql, int index, const char *path) {
    assert(index == FIMDB_STMT_SET_SCANNED || index == FIMDB_STMT_GET_PATH_FROM_PATTERN ||
           index == FIMDB_STMT_GET_PATH || index == FIMDB_STMT_DELETE_PATH || index == FIMDB_STMT_PATH_IS_SCANNED);
    sqlite3_bind_text(fim_sql->stmt[index], 1, path, -1, NULL);
}

/* FIMDB_STMT_GET_PATHS_INODE */
void fim_db_bind_get_inode(fdb_t *fim_sql, int index, unsigned long int inode, unsigned long int dev) {
    if (index == FIMDB_STMT_GET_PATHS_INODE || index == FIMDB_STMT_DATA_ROW_EXISTS) {
        sqlite3_bind_int64(fim_sql->stmt[index], 1, inode);
        sqlite3_bind_int(fim_sql->stmt[index], 2, dev);
    }
}

fim_entry *fim_db_get_path(fdb_t *fim_sql, const char *file_path) {
    fim_entry *entry = NULL;

    // Clean and bind statements
    fim_db_clean_stmt(fim_sql, FIMDB_STMT_GET_PATH);
    fim_db_bind_path(fim_sql, FIMDB_STMT_GET_PATH, file_path);

    if (sqlite3_step(fim_sql->stmt[FIMDB_STMT_GET_PATH]) == SQLITE_ROW) {
        entry = fim_db_decode_full_row(fim_sql->stmt[FIMDB_STMT_GET_PATH]);
    }

    return entry;
}

char **fim_db_get_paths_from_inode(fdb_t *fim_sql, unsigned long int inode, unsigned long int dev) {
    int i = 0;
    char **paths = NULL;

    // Clean statements
    fim_db_clean_stmt(fim_sql, FIMDB_STMT_GET_PATHS_INODE);
    fim_db_bind_get_inode(fim_sql, FIMDB_STMT_GET_PATHS_INODE, inode, dev);

    os_calloc(2, sizeof(char *), paths);

    for (i = 0; sqlite3_step(fim_sql->stmt[FIMDB_STMT_GET_PATHS_INODE]) == SQLITE_ROW; i++) {
        char *p;
        os_realloc(paths, (i + 2) * sizeof(char *), paths);

        p = (char *)sqlite3_column_text(fim_sql->stmt[FIMDB_STMT_GET_PATHS_INODE], 0);
        sqlite_strdup(p, paths[i]);
    }

    paths[i] = NULL;

    fim_db_check_transaction(fim_sql);

    return paths;
}

int fim_db_append_paths_from_inode(fdb_t *fim_sql,
                                   unsigned long int inode,
                                   unsigned long int dev,
                                   OSList *list,
                                   rb_tree *tree) {
    int i = 0;
    int appended = 0;

    assert(list != NULL);
    assert(tree != NULL);

    // Clean statements
    fim_db_clean_stmt(fim_sql, FIMDB_STMT_GET_PATHS_INODE);
    fim_db_bind_get_inode(fim_sql, FIMDB_STMT_GET_PATHS_INODE, inode, dev);

    for (i = 0; sqlite3_step(fim_sql->stmt[FIMDB_STMT_GET_PATHS_INODE]) == SQLITE_ROW; i++) {
        rb_node *leaf =
        rbtree_insert(tree, (char *)sqlite3_column_text(fim_sql->stmt[FIMDB_STMT_GET_PATHS_INODE], 0), NULL);

        if (leaf) {
            OSList_AddData(list, leaf->key);
            appended++;
        }
    }

    fim_db_check_transaction(fim_sql);

    return appended;
}

int fim_db_insert_entry(fdb_t *fim_sql, const char *file_path, const fim_file_data *entry) {
    int res;

    fim_db_clean_stmt(fim_sql, FIMDB_STMT_REPLACE_ENTRY);
    fim_db_bind_replace_entry(fim_sql, file_path, entry);

    if (res = sqlite3_step(fim_sql->stmt[FIMDB_STMT_REPLACE_ENTRY]), res != SQLITE_DONE) {
            merror("Step error replacing path '%s': %s", file_path, sqlite3_errmsg(fim_sql->db));
            return FIMDB_ERR;
    }

    return FIMDB_OK;
}

int fim_db_insert(fdb_t *fim_sql, const char *file_path, const fim_file_data *new, const fim_file_data *saved) {
    int res_entry;
    int nodes_count;

    // Add event, check if db is full
    if (!saved) {
        if (syscheck.file_limit_enabled) {
            nodes_count = fim_db_get_count_entries(syscheck.database);
            if (nodes_count < 0) {
                mwarn(FIM_DATABASE_NODES_COUNT_FAIL);
                return FIMDB_ERR;
            }
            if (nodes_count >= syscheck.file_limit) {
                fim_sql->full = true;
                mdebug1("Couldn't insert '%s' entry into DB. The DB is full, please check your configuration.",
                        file_path);
                return FIMDB_FULL;
            }
        }
    }

    res_entry = fim_db_insert_entry(fim_sql, file_path, new);

    fim_db_check_transaction(fim_sql);

    return res_entry;
}

int fim_db_remove_path(fdb_t *fim_sql, const char *path) {
    int state = FIMDB_ERR;

    // Clean and bind statement
    fim_db_clean_stmt(fim_sql, FIMDB_STMT_DELETE_PATH);
    fim_db_bind_path(fim_sql, FIMDB_STMT_DELETE_PATH, path);

    if (sqlite3_step(fim_sql->stmt[FIMDB_STMT_DELETE_PATH]) != SQLITE_DONE) {
        goto end;
    }

    fim_sql->full = false;
    state = FIMDB_OK;

end:
    fim_db_check_transaction(fim_sql);
    return state;
}

void fim_db_remove_validated_path(fdb_t *fim_sql,
                                  fim_entry *entry,
                                  pthread_mutex_t *mutex,
                                  void *evt_data,
                                  void *configuration,
                                  __attribute__((unused)) void *_unused_patameter) {
    const directory_t *original_configuration = (const directory_t *)configuration;
    directory_t *validated_configuration = fim_configuration_directory(entry->file_entry.path);

    if (validated_configuration == original_configuration) {
        fim_delete_file_event(fim_sql, entry, mutex, evt_data, NULL, NULL);
    }
}

int fim_db_set_all_unscanned(fdb_t *fim_sql) {
    int retval = fim_db_exec_simple_wquery(fim_sql, SQL_STMT[FIMDB_STMT_SET_ALL_UNSCANNED]);
    fim_db_check_transaction(fim_sql);
    return retval;
}

int fim_db_set_scanned(fdb_t *fim_sql, const char *path) {
    // Clean and bind statements
    fim_db_clean_stmt(fim_sql, FIMDB_STMT_SET_SCANNED);
    fim_db_bind_set_scanned(fim_sql, path);

    if (sqlite3_step(fim_sql->stmt[FIMDB_STMT_SET_SCANNED]) != SQLITE_DONE) {
        merror("Step error setting scanned path '%s': %s", path, sqlite3_errmsg(fim_sql->db));
        return FIMDB_ERR;
    }

    fim_db_check_transaction(fim_sql);

    return FIMDB_OK;
}

int fim_db_get_count_file_inode(fdb_t * fim_sql) {
    int res = fim_db_get_count(fim_sql, FIMDB_STMT_GET_COUNT_INODE);

    if(res == FIMDB_ERR) {
        merror("Step error getting count entry data: %s", sqlite3_errmsg(fim_sql->db));
    }
    return res;
}

int fim_db_get_count_file_entry(fdb_t * fim_sql) {
    int res = fim_db_get_count(fim_sql, FIMDB_STMT_GET_COUNT_PATH);

    if(res == FIMDB_ERR) {
        merror("Step error getting count entry path: %s", sqlite3_errmsg(fim_sql->db));
    }
    return res;
}

int fim_db_get_path_from_pattern(fdb_t *fim_sql, const char *pattern, fim_tmp_file **file, int storage) {
    if ((*file = fim_db_create_temp_file(storage)) == NULL) {
        return FIMDB_ERR;
    }

    fim_db_clean_stmt(fim_sql, FIMDB_STMT_GET_PATH_FROM_PATTERN);
    fim_db_bind_get_path_from_pattern(fim_sql, pattern);

    int ret = fim_db_multiple_row_query(fim_sql, FIMDB_STMT_GET_PATH_FROM_PATTERN,
                                        FIM_DB_DECODE_TYPE(fim_db_decode_string), free,
                                        FIM_DB_CALLBACK_TYPE(fim_db_callback_save_string),
                                        storage, (void *)*file);
    if (*file && (*file)->elements == 0) {
        fim_db_clean_file(file, storage);
    }

    return ret;
}

int fim_db_data_exists(fdb_t *fim_sql, unsigned long int inode, unsigned long int dev) {
    fim_db_clean_stmt(fim_sql, FIMDB_STMT_DATA_ROW_EXISTS);
    fim_db_bind_get_inode(fim_sql, FIMDB_STMT_DATA_ROW_EXISTS, inode, dev);

    if (sqlite3_step(fim_sql->stmt[FIMDB_STMT_DATA_ROW_EXISTS]) != SQLITE_ROW) {
        return FIMDB_ERR;
    }
    return sqlite3_column_int(fim_sql->stmt[FIMDB_STMT_DATA_ROW_EXISTS], 0);
}

int fim_db_file_is_scanned(fdb_t *fim_sql, const char *path) {
    fim_db_clean_stmt(fim_sql, FIMDB_STMT_PATH_IS_SCANNED);
    fim_db_bind_path(fim_sql, FIMDB_STMT_PATH_IS_SCANNED, path);

    switch (sqlite3_step(fim_sql->stmt[FIMDB_STMT_PATH_IS_SCANNED])) {
    case SQLITE_ROW:
        return sqlite3_column_int(fim_sql->stmt[FIMDB_STMT_PATH_IS_SCANNED], 0);
    case SQLITE_DONE:
        return 0;
    case SQLITE_ERROR:
        mdebug2(FIM_DB_FAIL_TO_GET_SCANNED_FILE, path, sqlite3_errmsg(fim_sql->db));
        // Fallthrough
    default:
        return FIMDB_ERR;
    }
}
