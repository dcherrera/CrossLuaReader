/**
 * @file bridge_storage.cpp
 * @brief C++ bridge wrapping SDCardManager SDK class for C HAL access.
 *        File handles are exposed as opaque void* pointers since C cannot
 *        see the FsFile class.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include <SDCardManager.h>

extern "C" {

#define SD_CS_PIN 12

static bool sd_initialized = false;

bool bridge_storage_init(void) {
    if (sd_initialized) return true;
    sd_initialized = SDCardManager::getInstance().begin();
    return sd_initialized;
}

bool bridge_storage_ready(void) {
    return SDCardManager::getInstance().ready();
}

bool bridge_storage_exists(const char *path) {
    return SDCardManager::getInstance().exists(path);
}

bool bridge_storage_mkdir(const char *path) {
    return SDCardManager::getInstance().mkdir(path, true);
}

bool bridge_storage_remove(const char *path) {
    return SDCardManager::getInstance().remove(path);
}

bool bridge_storage_rename(const char *old_path, const char *new_path) {
    return SDCardManager::getInstance().rename(old_path, new_path);
}

bool bridge_storage_ensure_dir(const char *path) {
    return SDCardManager::getInstance().ensureDirectoryExists(path);
}

/**
 * Open a file and return an opaque handle.
 * The handle is a heap-allocated FsFile.
 *
 * @param path File path on SD card
 * @param mode 0 = read, 1 = write (create/truncate)
 * @return Opaque handle, or NULL on failure
 */
void *bridge_storage_open(const char *path, int mode) {
    oflag_t flags = (mode == 1) ? (O_WRONLY | O_CREAT | O_TRUNC) : O_RDONLY;
    FsFile *file = new FsFile();
    *file = SDCardManager::getInstance().open(path, flags);
    if (!*file) {
        delete file;
        return NULL;
    }
    return static_cast<void *>(file);
}

int bridge_storage_file_read(void *handle, void *buf, size_t count) {
    if (!handle) return -1;
    FsFile *file = static_cast<FsFile *>(handle);
    return file->read(buf, count);
}

size_t bridge_storage_file_write(void *handle, const void *buf, size_t count) {
    if (!handle) return 0;
    FsFile *file = static_cast<FsFile *>(handle);
    return file->write(static_cast<const uint8_t *>(buf), count);
}

bool bridge_storage_file_seek(void *handle, size_t pos) {
    if (!handle) return false;
    FsFile *file = static_cast<FsFile *>(handle);
    return file->seek(pos);
}

size_t bridge_storage_file_position(void *handle) {
    if (!handle) return 0;
    FsFile *file = static_cast<FsFile *>(handle);
    return file->position();
}

size_t bridge_storage_file_size(void *handle) {
    if (!handle) return 0;
    FsFile *file = static_cast<FsFile *>(handle);
    return file->size();
}

int bridge_storage_file_available(void *handle) {
    if (!handle) return 0;
    FsFile *file = static_cast<FsFile *>(handle);
    return file->available();
}

void bridge_storage_file_close(void *handle) {
    if (!handle) return;
    FsFile *file = static_cast<FsFile *>(handle);
    file->close();
    delete file;
}

/**
 * Open a directory for iteration.
 *
 * @param path Directory path
 * @return Opaque directory handle, or NULL on failure
 */
void *bridge_storage_dir_open(const char *path) {
    FsFile *dir = new FsFile();
    *dir = SDCardManager::getInstance().open(path, O_RDONLY);
    if (!*dir || !dir->isDirectory()) {
        delete dir;
        return NULL;
    }
    return static_cast<void *>(dir);
}

/**
 * Read next entry from directory.
 *
 * @param handle Directory handle from bridge_storage_dir_open
 * @param name_buf Buffer to receive entry name
 * @param buf_size Size of name buffer
 * @param is_dir Set to true if entry is a directory
 * @return true if an entry was read, false if end of directory
 */
bool bridge_storage_dir_next(void *handle, char *name_buf, size_t buf_size, bool *is_dir) {
    if (!handle) return false;
    FsFile *dir = static_cast<FsFile *>(handle);
    FsFile entry = dir->openNextFile();
    if (!entry) return false;

    entry.getName(name_buf, buf_size);
    *is_dir = entry.isDirectory();
    entry.close();
    return true;
}

void bridge_storage_dir_close(void *handle) {
    if (!handle) return;
    FsFile *dir = static_cast<FsFile *>(handle);
    dir->close();
    delete dir;
}

}  /* extern "C" */
