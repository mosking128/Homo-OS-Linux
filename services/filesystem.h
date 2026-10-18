#ifndef HACHIMI_FILESYSTEM_SERVICE_H
#define HACHIMI_FILESYSTEM_SERVICE_H
#include <stdbool.h>
#include <stddef.h>
#define HACHIMI_FS_PATH_MAX 1024
#define HACHIMI_FS_NAME_MAX 256
typedef struct {
    char name[HACHIMI_FS_NAME_MAX];
    bool directory;
    unsigned long long size;
} hachimi_fs_entry_t;
bool hachimi_fs_exists(const char * path);
bool hachimi_fs_is_directory(const char * path);
bool hachimi_fs_is_link(const char * path);
bool hachimi_fs_get_size(const char * path, unsigned long long * size);
bool hachimi_fs_join(const char * directory, const char * name, char * output, size_t output_size);
bool hachimi_fs_valid_name(const char * name);
bool hachimi_fs_home(char * path, size_t capacity);
bool hachimi_fs_list(const char * path, hachimi_fs_entry_t * entries, size_t capacity, size_t * count);
bool hachimi_fs_read_text(const char * path, char * output, size_t capacity, size_t * length);
bool hachimi_fs_read_text_range(const char * path, unsigned long long offset,
                               char * output, size_t capacity, size_t * length);
bool hachimi_fs_write_text(const char * path, const char * text, bool create_only);
bool hachimi_fs_mkdir(const char * path);
bool hachimi_fs_rename(const char * old_path, const char * new_path);
/* Does not follow symlinks when recursively removing a directory. */
bool hachimi_fs_remove(const char * path);
#endif
