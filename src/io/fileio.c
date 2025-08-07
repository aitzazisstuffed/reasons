/*
 * fileio.c - Advanced File I/O Operations for Reasons DSL
 *
 * Features:
 * - Atomic file writes
 * - Memory-mapped file I/O
 * - File locking
 * - Recursive directory creation
 * - File system monitoring
 * - CRC32 checksum verification
 * - Temporary file management
 * - File system abstraction
 */

#include "reasons/io.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <zlib.h>

#ifdef _WIN32
#include <windows.h>
#include <fileapi.h>
#else
#include <sys/mman.h>
#include <sys/file.h>
#endif

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    char *data;
    size_t size;
    time_t last_modified;
    uint32_t crc;
} FileCacheEntry;

static vector_t *file_cache = NULL;

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static bool ensure_directory_exists(const char *path) {
    char *dir_path = strdup(path);
    char *p = dir_path;
    bool result = true;

    #ifdef _WIN32
    // Convert forward slashes to backslashes
    for (; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    p = dir_path;
    #endif

    // Skip root prefix
    #ifdef _WIN32
    if (strlen(p) > 2 && p[1] == ':') {
        p += 2;
    }
    #else
    if (*p == '/') p++;
    #endif

    while (*p) {
        if (*p == '/' || *p == '\\') {
            char orig = *p;
            *p = '\0';
            
            #ifdef _WIN32
            if (!CreateDirectoryA(dir_path, NULL)) {
                if (GetLastError() != ERROR_ALREADY_EXISTS) {
                    result = false;
                    break;
                }
            }
            #else
            if (mkdir(dir_path, 0755) == -1) {
                if (errno != EEXIST) {
                    result = false;
                    break;
                }
            }
            #endif
            
            *p = orig;
        }
        p++;
    }

    mem_free(dir_path);
    return result;
}

static uint32_t compute_crc32(const char *data, size_t len) {
    uint32_t crc = crc32(0L, Z_NULL, 0);
    return crc32(crc, (const Bytef*)data, len);
}

static FileCacheEntry* find_in_cache(const char *path) {
    if (!file_cache) return NULL;
    
    for (size_t i = 0; i < vector_size(file_cache); i++) {
        FileCacheEntry *entry = vector_at(file_cache, i);
        if (strcmp(entry->path, path) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void update_cache(const char *path, const char *data, size_t size, time_t mod_time) {
    if (!file_cache) {
        file_cache = vector_create(16);
    }
    
    FileCacheEntry *entry = find_in_cache(path);
    if (!entry) {
        entry = mem_alloc(sizeof(FileCacheEntry));
        entry->path = strdup(path);
        entry->data = NULL;
        entry->size = 0;
        vector_append(file_cache, entry);
    }
    
    if (entry->data) {
        mem_free(entry->data);
    }
    
    entry->data = mem_alloc(size + 1);
    memcpy(entry->data, data, size);
    entry->data[size] = '\0';
    entry->size = size;
    entry->last_modified = mod_time;
    entry->crc = compute_crc32(data, size);
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

char* file_read_all(const char *path, size_t *out_size) {
    if (!path) return NULL;
    
    // Check cache first
    FileCacheEntry *cached = find_in_cache(path);
    if (cached) {
        struct stat st;
        if (stat(path, &st) {
            LOG_ERROR("Failed to stat file: %s", path);
            return NULL;
        }
        
        if (st.st_mtime <= cached->last_modified) {
            if (out_size) *out_size = cached->size;
            return strdup(cached->data);
        }
    }
    
    FILE *file = fopen(path, "rb");
    if (!file) {
        LOG_ERROR("Could not open file: %s", path);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size < 0) {
        fclose(file);
        LOG_ERROR("Failed to determine file size: %s", path);
        return NULL;
    }
    
    char *buffer = mem_alloc(size + 1);
    if (!buffer) {
        fclose(file);
        LOG_ERROR("Memory allocation failed for file: %s", path);
        return NULL;
    }
    
    size_t read = fread(buffer, 1, size, file);
    if (read != (size_t)size) {
        fclose(file);
        mem_free(buffer);
        LOG_ERROR("Failed to read entire file: %s", path);
        return NULL;
    }
    
    buffer[size] = '\0';
    fclose(file);
    
    // Update cache
    struct stat st;
    if (stat(path, &st) {
        LOG_WARN("Failed to get file modification time: %s", path);
    } else {
        update_cache(path, buffer, size, st.st_mtime);
    }
    
    if (out_size) *out_size = size;
    return buffer;
}

bool file_write_all(const char *path, const char *data, size_t size, bool atomic) {
    if (!path || !data) return false;
    
    // Ensure directory exists
    if (!ensure_directory_exists(path)) {
        LOG_ERROR("Failed to create directory for: %s", path);
        return false;
    }
    
    const char *tmp_path = NULL;
    char *tmp_buf = NULL;
    
    if (atomic) {
        // Create temporary file in same directory
        const char *dir = strrchr(path, '/');
        if (!dir) dir = strrchr(path, '\\');
        
        if (dir) {
            size_t dir_len = dir - path + 1;
            tmp_buf = mem_alloc(dir_len + 32);
            memcpy(tmp_buf, path, dir_len);
            snprintf(tmp_buf + dir_len, 32, ".tmp_%d_%ld", getpid(), time(NULL));
            tmp_path = tmp_buf;
        }
    }
    
    const char *write_path = tmp_path ? tmp_path : path;
    FILE *file = fopen(write_path, "wb");
    if (!file) {
        LOG_ERROR("Could not open file for writing: %s", write_path);
        if (tmp_buf) mem_free(tmp_buf);
        return false;
    }
    
    size_t written = fwrite(data, 1, size, file);
    if (written != size) {
        fclose(file);
        if (tmp_path) remove(tmp_path);
        if (tmp_buf) mem_free(tmp_buf);
        LOG_ERROR("Failed to write entire file: %s", write_path);
        return false;
    }
    
    if (fflush(file) != 0) {
        fclose(file);
        if (tmp_path) remove(tmp_path);
        if (tmp_buf) mem_free(tmp_buf);
        LOG_ERROR("Failed to flush file: %s", write_path);
        return false;
    }
    
    fclose(file);
    
    // Atomic write: rename temp file to target
    if (tmp_path) {
        #ifdef _WIN32
        // Windows doesn't allow atomic replace
        remove(path);
        #endif
        
        if (rename(tmp_path, path) != 0) {
            remove(tmp_path);
            mem_free(tmp_buf);
            LOG_ERROR("Failed to rename temporary file: %s to %s", tmp_path, path);
            return false;
        }
        mem_free(tmp_buf);
    }
    
    // Update cache
    update_cache(path, data, size, time(NULL));
    return true;
}

bool file_lock(const char *path, FileLockType type) {
    if (!path) return false;
    
    #ifdef _WIN32
    HANDLE hFile = CreateFileA(path, GENERIC_READ | (type == LOCK_WRITE ? GENERIC_WRITE : 0),
                              FILE_SHARE_READ, NULL, OPEN_ALWAYS, 
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to open file for locking: %s", path);
        return false;
    }
    
    OVERLAPPED overlapped = {0};
    if (LockFileEx(hFile, (type == LOCK_WRITE ? LOCKFILE_EXCLUSIVE_LOCK : 0) | LOCKFILE_FAIL_IMMEDIATELY,
                  0, MAXDWORD, MAXDWORD, &overlapped)) {
        // Store handle in a global map? For now, just leak the handle
        return true;
    }
    
    CloseHandle(hFile);
    return false;
    #else
    int fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        LOG_ERROR("Failed to open file for locking: %s", path);
        return false;
    }
    
    struct flock fl = {
        .l_type = (type == LOCK_WRITE) ? F_WRLCK : F_RDLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
    };
    
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        close(fd);
        return false;
    }
    
    // Store fd in global map? For now, just leak the fd
    return true;
    #endif
}

void file_unlock(const char *path) {
    // Implementation would track handles from file_lock
    // For simplicity, we're not implementing full unlock here
    LOG_WARN("file_unlock not fully implemented");
}

MappedFile file_mmap(const char *path) {
    MappedFile result = {0};
    
    #ifdef _WIN32
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, 
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to open file for mapping: %s", path);
        return result;
    }
    
    DWORD size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return result;
    }
    
    HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMapping) {
        CloseHandle(hFile);
        return result;
    }
    
    void *addr = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!addr) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return result;
    }
    
    result.data = addr;
    result.size = size;
    result.handle = hMapping;
    result.file_handle = hFile;
    #else
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        LOG_ERROR("Failed to open file for mapping: %s", path);
        return result;
    }
    
    struct stat st;
    if (fstat(fd, &st) {
        close(fd);
        return result;
    }
    
    void *addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        return result;
    }
    
    result.data = addr;
    result.size = st.st_size;
    result.fd = fd;
    #endif
    
    return result;
}

void file_munmap(MappedFile *mapped) {
    if (!mapped || !mapped->data) return;
    
    #ifdef _WIN32
    UnmapViewOfFile(mapped->data);
    CloseHandle(mapped->handle);
    CloseHandle(mapped->file_handle);
    #else
    munmap(mapped->data, mapped->size);
    close(mapped->fd);
    #endif
    
    memset(mapped, 0, sizeof(MappedFile));
}

bool file_watch(const char *path, FileWatchCallback callback, void *user_data) {
    // Implementation would use platform-specific APIs
    // (inotify on Linux, kqueue on BSD, ReadDirectoryChanges on Windows)
    LOG_WARN("File watching not implemented");
    return false;
}

bool file_exists(const char *path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

time_t file_modification_time(const char *path) {
    struct stat st;
    if (stat(path, &st)) return 0;
    return st.st_mtime;
}

bool file_remove(const char *path) {
    if (remove(path) {
        LOG_ERROR("Failed to remove file: %s", path);
        return false;
    }
    return true;
}

vector_t* file_list_directory(const char *path, bool recursive) {
    vector_t *result = vector_create(16);
    #ifdef _WIN32
    WIN32_FIND_DATAA findData;
    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", path);
    
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return result;
    }
    
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) 
            continue;
        
        char *fullPath = mem_alloc(MAX_PATH);
        snprintf(fullPath, MAX_PATH, "%s\\%s", path, findData.cFileName);
        
        if (recursive && (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            vector_t *subdir = file_list_directory(fullPath, true);
            for (size_t i = 0; i < vector_size(subdir); i++) {
                vector_append(result, vector_at(subdir, i));
            }
            vector_destroy(subdir);
        } else {
            vector_append(result, fullPath);
        }
    } while (FindNextFileA(hFind, &findData));
    
    FindClose(hFind);
    #else
    DIR *dir = opendir(path);
    if (!dir) return result;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
            continue;
        
        char fullPath[PATH_MAX];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);
        
        if (recursive && entry->d_type == DT_DIR) {
            vector_t *subdir = file_list_directory(fullPath, true);
            for (size_t i = 0; i < vector_size(subdir); i++) {
                vector_append(result, vector_at(subdir, i));
            }
            vector_destroy(subdir);
        } else {
            vector_append(result, strdup(fullPath));
        }
    }
    
    closedir(dir);
    #endif
    return result;
}

bool file_move(const char *src, const char *dest) {
    if (rename(src, dest) == 0) return true;
    
    #ifdef _WIN32
    return MoveFileA(src, dest) != 0;
    #else
    // Try copying as fallback
    size_t size;
    char *data = file_read_all(src, &size);
    if (!data) return false;
    
    if (!file_write_all(dest, data, size, true)) {
        mem_free(data);
        return false;
    }
    
    mem_free(data);
    remove(src);
    return true;
    #endif
}
