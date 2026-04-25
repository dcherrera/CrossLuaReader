/**
 * @file hal_storage.h
 * @brief SD card storage API: file I/O and directory operations.
 *        Uses opaque handles since the underlying FsFile class is C++.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Opaque file handle. */
typedef void *hal_file_t;

/** Opaque directory handle. */
typedef void *hal_dir_t;

/** File open modes. */
#define HAL_FILE_READ  0
#define HAL_FILE_WRITE 1

/**
 * Initialize SD card. Must be called after hal_gpio_init() (shared SPI).
 *
 * @return true if SD card mounted successfully
 */
bool hal_storage_init(void);

/** @return true if SD card is mounted and ready. */
bool hal_storage_ready(void);

/** @return true if path exists on SD card. */
bool hal_storage_exists(const char *path);

/** Create directory (and parents). @return true on success. */
bool hal_storage_mkdir(const char *path);

/** Delete a file. @return true on success. */
bool hal_storage_remove(const char *path);

/** Rename/move a file. @return true on success. */
bool hal_storage_rename(const char *old_path, const char *new_path);

/**
 * Open a file.
 *
 * @param path File path on SD card
 * @param mode HAL_FILE_READ or HAL_FILE_WRITE
 * @return File handle, or NULL on failure
 */
hal_file_t hal_storage_open(const char *path, int mode);

/**
 * Read bytes from an open file.
 *
 * @param file File handle
 * @param buf Destination buffer
 * @param count Max bytes to read
 * @return Bytes actually read, or -1 on error
 */
int hal_storage_file_read(hal_file_t file, void *buf, size_t count);

/**
 * Write bytes to an open file.
 *
 * @param file File handle
 * @param buf Source buffer
 * @param count Bytes to write
 * @return Bytes actually written
 */
size_t hal_storage_file_write(hal_file_t file, const void *buf, size_t count);

/** Seek to position in file. @return true on success. */
bool hal_storage_file_seek(hal_file_t file, size_t pos);

/** @return Current read/write position. */
size_t hal_storage_file_position(hal_file_t file);

/** @return Total file size in bytes. */
size_t hal_storage_file_size(hal_file_t file);

/** @return Bytes available to read. */
int hal_storage_file_available(hal_file_t file);

/** Close file and free handle. */
void hal_storage_file_close(hal_file_t file);

/**
 * Open a directory for iteration.
 *
 * @param path Directory path
 * @return Directory handle, or NULL on failure
 */
hal_dir_t hal_storage_dir_open(const char *path);

/**
 * Read next directory entry.
 *
 * @param dir Directory handle
 * @param name_buf Buffer to receive entry name
 * @param buf_size Size of name buffer
 * @param is_dir Set to true if entry is a directory
 * @return true if entry was read, false if end of directory
 */
bool hal_storage_dir_next(hal_dir_t dir, char *name_buf, size_t buf_size, bool *is_dir);

/** Close directory handle. */
void hal_storage_dir_close(hal_dir_t dir);
