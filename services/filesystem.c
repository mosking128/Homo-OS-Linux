#include "filesystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define FILES_PATH_SEP '\\'
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#define FILES_PATH_SEP '/'
#endif


#define FILES_ROOT_SIZE HACHIMI_FS_PATH_MAX
#define FILES_NAME_SIZE HACHIMI_FS_NAME_MAX
typedef hachimi_fs_entry_t file_entry_t;

bool hachimi_fs_exists(const char * path)
{
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info;
    return lstat(path, &info) == 0;
#endif
}

bool hachimi_fs_is_directory(const char * path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    struct stat info;
    return lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

bool hachimi_fs_is_link(const char * path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat info;
    return lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
#endif
}

bool hachimi_fs_get_size(const char * path, unsigned long long * size)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if(!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return false;
    *size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    return true;
#else
    struct stat info;
    if(lstat(path, &info) != 0) return false;
    *size = (unsigned long long)info.st_size;
    return true;
#endif
}

bool hachimi_fs_join(const char * directory, const char * name,
                      char * output, size_t output_size)
{
    size_t length = strlen(directory);
    bool needs_separator = length > 0 && directory[length - 1] != '/' &&
                           directory[length - 1] != '\\';
    int result = snprintf(output, output_size, "%s%s%s", directory,
                          needs_separator ? (FILES_PATH_SEP == '/' ? "/" : "\\") : "",
                          name);
    return result > 0 && (size_t)result < output_size;
}

bool hachimi_fs_valid_name(const char * name)
{
    if(name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 ||
       strcmp(name, "..") == 0 || strlen(name) >= FILES_NAME_SIZE) return false;
    for(const unsigned char * p = (const unsigned char *)name; *p; p++) {
        if(*p < 32 || *p == '/' || *p == '\\') return false;
#ifdef _WIN32
        if(*p == ':' || *p == '*' || *p == '?' || *p == '"' ||
           *p == '<' || *p == '>' || *p == '|') return false;
#endif
    }
    return true;
}

static int entry_compare(const void * left, const void * right)
{
    const file_entry_t * a = left;
    const file_entry_t * b = right;
    if(a->directory != b->directory) return a->directory ? -1 : 1;
    return strcmp(a->name, b->name);
}

bool hachimi_fs_remove(const char * path)
{
    if(hachimi_fs_is_link(path)) {
#ifdef _WIN32
        DWORD attributes = GetFileAttributesA(path);
        if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return RemoveDirectoryA(path) != 0;
        return DeleteFileA(path) != 0;
#else
        return unlink(path) == 0;
#endif
    }
    if(!hachimi_fs_is_directory(path)) {
#ifdef _WIN32
        return _unlink(path) == 0;
#else
        return unlink(path) == 0;
#endif
    }
#ifdef _WIN32
    char pattern[FILES_ROOT_SIZE];
    if(!hachimi_fs_join(path, "*", pattern, sizeof(pattern))) return false;
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    if(handle != INVALID_HANDLE_VALUE) {
        do {
            if(strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
            char child[FILES_ROOT_SIZE];
            if(!hachimi_fs_join(path, data.cFileName, child, sizeof(child)) || !hachimi_fs_remove(child)) {
                FindClose(handle);
                return false;
            }
        } while(FindNextFileA(handle, &data));
        FindClose(handle);
    }
    return RemoveDirectoryA(path) != 0;
#else
    DIR * directory = opendir(path);
    if(directory == NULL) return false;
    struct dirent * item;
    while((item = readdir(directory)) != NULL) {
        if(strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
        char child[FILES_ROOT_SIZE];
        if(!hachimi_fs_join(path, item->d_name, child, sizeof(child)) || !hachimi_fs_remove(child)) {
            closedir(directory);
            return false;
        }
    }
    closedir(directory);
    return rmdir(path) == 0;
#endif
}


bool hachimi_fs_home(char * path, size_t capacity)
{
#ifdef _WIN32
    const char * home = getenv("USERPROFILE");
    const char * fallback = ".";
#else
    const char * home = getenv("HOME");
    const char * fallback = "/tmp";
#endif
    if(home == NULL || !home[0] || !hachimi_fs_is_directory(home)) home = fallback;
    int length = snprintf(path, capacity, "%s", home);
    return length > 0 && (size_t)length < capacity;
}

bool hachimi_fs_list(const char * path, hachimi_fs_entry_t * entries, size_t capacity, size_t * out_count)
{
    size_t count = 0;
    *out_count = 0;
#ifdef _WIN32
    char pattern[FILES_ROOT_SIZE];
    if(hachimi_fs_join(path, "*", pattern, sizeof(pattern))) {
        WIN32_FIND_DATAA data;
        HANDLE handle = FindFirstFileA(pattern, &data);
        if(handle == INVALID_HANDLE_VALUE) return false;
        if(handle != INVALID_HANDLE_VALUE) {
            do {
                if(strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0 ||
                   data.cFileName[0] == '.' || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                if(count >= capacity) break;
                if(strlen(data.cFileName) >= HACHIMI_FS_NAME_MAX) continue;
                file_entry_t * entry = &entries[count++];
                memcpy(entry->name, data.cFileName, strlen(data.cFileName) + 1);
                entry->directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entry->size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            } while(FindNextFileA(handle, &data));
            FindClose(handle);
        }
    }
#else
    DIR * directory = opendir(path);
    if(directory == NULL) return false;
    if(directory != NULL) {
        struct dirent * item;
        while((item = readdir(directory)) != NULL && count < capacity) {
            if(strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0 ||
               item->d_name[0] == '.') continue;
            char child[FILES_ROOT_SIZE];
            if(!hachimi_fs_join(path, item->d_name, child, sizeof(child)) || hachimi_fs_is_link(child)) continue;
            unsigned long long size = 0;
            if(!hachimi_fs_get_size(child, &size)) continue;
            file_entry_t * entry = &entries[count++];
            snprintf(entry->name, sizeof(entry->name), "%s", item->d_name);
            entry->directory = hachimi_fs_is_directory(child);
            entry->size = size;
        }
        closedir(directory);
    }
#endif


    qsort(entries, count, sizeof(*entries), entry_compare);
    *out_count = count;
    return true;
}

bool hachimi_fs_read_text(const char * path, char * output, size_t capacity, size_t * length)
{
    if(capacity == 0 || hachimi_fs_is_link(path)) return false;
    FILE * file = fopen(path, "rb");
    if(file == NULL) return false;
    *length = fread(output, 1, capacity - 1, file);
    output[*length] = '\0';
    bool success = !ferror(file);
    fclose(file);
    return success;
}

bool hachimi_fs_read_text_range(const char * path, unsigned long long offset,
                                char * output, size_t capacity, size_t * length)
{
    if(capacity == 0 || hachimi_fs_is_link(path)) return false;
    FILE * file = fopen(path, "rb");
    if(file == NULL) return false;
    bool success = false;
    if(offset <= (unsigned long long)LONG_MAX && fseek(file, (long)offset, SEEK_SET) == 0) {
        *length = fread(output, 1, capacity - 1, file);
        output[*length] = '\0';
        success = !ferror(file);
    }
    fclose(file);
    return success;
}

bool hachimi_fs_write_text(const char * path, const char * text, bool create_only)
{
    if(hachimi_fs_is_link(path)) return false;
#ifdef _WIN32
    int descriptor = _open(path, _O_WRONLY | _O_BINARY | (create_only ? _O_CREAT | _O_EXCL : _O_TRUNC), _S_IREAD | _S_IWRITE);
    FILE * file = descriptor < 0 ? NULL : _fdopen(descriptor, "wb");
    if(descriptor >= 0 && file == NULL) _close(descriptor);
#else
    int descriptor = open(path, O_WRONLY | O_NOFOLLOW | (create_only ? O_CREAT | O_EXCL : O_TRUNC), 0644);
    FILE * file = descriptor < 0 ? NULL : fdopen(descriptor, "wb");
    if(descriptor >= 0 && file == NULL) close(descriptor);
#endif
    if(file == NULL) return false;
    size_t length = strlen(text);
    bool success = fwrite(text, 1, length, file) == length;
    if(fclose(file) != 0) success = false;
    return success;
}

bool hachimi_fs_mkdir(const char * path)
{
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
}

bool hachimi_fs_rename(const char * old_path, const char * new_path)
{
    if(hachimi_fs_exists(new_path)) return false;
    return rename(old_path, new_path) == 0;
}
