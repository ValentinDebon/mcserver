#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

char *storage_version_manifest_path(void);

char *storage_archive_path(const char *id);

char *storage_world_directory(const char *world);

void storage_fetch(const char *path, const char *url);

void storage_fetch_and_verify(const char *path, const char *url, const char *sha1, size_t expected_size);

/* STORAGE_H */
#endif
