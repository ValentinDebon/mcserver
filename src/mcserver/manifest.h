#ifndef MANIFEST_H
#define MANIFEST_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "version.h"

struct server_archive {
	char *url;
	size_t size;
	uint8_t sha1[20];
};

void
manifest_init(const char *url, time_t ttu);

const char *
manifest_resolve(enum version version, const char *id);

void
manifest_server_archive_fetch(enum version version, const char *id, struct server_archive *archive);

void
manifest_server_archive_cleanup(struct server_archive *archive);

/* MANIFEST_H */
#endif
