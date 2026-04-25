/**
 * @file hal_storage.c
 * @brief SD card storage implementation. Wraps bridge functions with logging.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "hal_storage.h"
#include "logging.h"

/* Bridge function declarations */
extern bool bridge_storage_init(void);
extern bool bridge_storage_ready(void);
extern bool bridge_storage_exists(const char *path);
extern bool bridge_storage_mkdir(const char *path);
extern bool bridge_storage_remove(const char *path);
extern bool bridge_storage_rename(const char *old_path, const char *new_path);
extern void *bridge_storage_open(const char *path, int mode);
extern int bridge_storage_file_read(void *handle, void *buf, size_t count);
extern size_t bridge_storage_file_write(void *handle, const void *buf, size_t count);
extern bool bridge_storage_file_seek(void *handle, size_t pos);
extern size_t bridge_storage_file_position(void *handle);
extern size_t bridge_storage_file_size(void *handle);
extern int bridge_storage_file_available(void *handle);
extern void bridge_storage_file_close(void *handle);
extern void *bridge_storage_dir_open(const char *path);
extern bool bridge_storage_dir_next(void *handle, char *name_buf, size_t buf_size, bool *is_dir);
extern void bridge_storage_dir_close(void *handle);

bool hal_storage_init(void) {
    LOG_INF("SD", "Initializing SD card");
    bool ok = bridge_storage_init();
    if (!ok) {
        LOG_ERR("SD", "SD card init failed");
    } else {
        LOG_INF("SD", "SD card ready");
    }
    return ok;
}

bool hal_storage_ready(void) {
    return bridge_storage_ready();
}

bool hal_storage_exists(const char *path) {
    return bridge_storage_exists(path);
}

bool hal_storage_mkdir(const char *path) {
    return bridge_storage_mkdir(path);
}

bool hal_storage_remove(const char *path) {
    return bridge_storage_remove(path);
}

bool hal_storage_rename(const char *old_path, const char *new_path) {
    return bridge_storage_rename(old_path, new_path);
}

hal_file_t hal_storage_open(const char *path, int mode) {
    void *handle = bridge_storage_open(path, mode);
    if (!handle) {
        LOG_ERR("SD", "Failed to open: %s (mode %d)", path, mode);
    }
    return handle;
}

int hal_storage_file_read(hal_file_t file, void *buf, size_t count) {
    return bridge_storage_file_read(file, buf, count);
}

size_t hal_storage_file_write(hal_file_t file, const void *buf, size_t count) {
    return bridge_storage_file_write(file, buf, count);
}

bool hal_storage_file_seek(hal_file_t file, size_t pos) {
    return bridge_storage_file_seek(file, pos);
}

size_t hal_storage_file_position(hal_file_t file) {
    return bridge_storage_file_position(file);
}

size_t hal_storage_file_size(hal_file_t file) {
    return bridge_storage_file_size(file);
}

int hal_storage_file_available(hal_file_t file) {
    return bridge_storage_file_available(file);
}

void hal_storage_file_close(hal_file_t file) {
    bridge_storage_file_close(file);
}

hal_dir_t hal_storage_dir_open(const char *path) {
    void *handle = bridge_storage_dir_open(path);
    if (!handle) {
        LOG_ERR("SD", "Failed to open dir: %s", path);
    }
    return handle;
}

bool hal_storage_dir_next(hal_dir_t dir, char *name_buf, size_t buf_size, bool *is_dir) {
    return bridge_storage_dir_next(dir, name_buf, buf_size, is_dir);
}

void hal_storage_dir_close(hal_dir_t dir) {
    bridge_storage_dir_close(dir);
}
