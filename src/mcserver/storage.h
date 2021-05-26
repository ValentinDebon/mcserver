#ifndef STORAGE_H
#define STORAGE_H

#include <time.h>

#include "version.h"
#include "manifest.h"

void
storage_init(const char *dir);

const char *
storage_version_manifest_path(void);

const char *
storage_server_archive_path(enum version version, const char *id);

void
storage_download_server_archive(enum version version, const char *id, const struct server_archive *archive);

/* STORAGE_H */
#endif
