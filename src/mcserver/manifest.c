#include "manifest.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

#include <stdbool.h>
#include <sys/stat.h>

#include <json-c/json.h>
#include <curl/curl.h>

#include "storage.h"

struct fetch_and_decode_json {
	struct json_tokener *tokener;
	struct json_object *object;
};

static size_t
fetch_and_decode_json_write(const void *data,
	size_t one, size_t count, struct fetch_and_decode_json *context) {

	/* Discard bytes after one object was parsed, abort if an error occured. */
	if (context->object == NULL) {
		struct json_tokener * const tokener = context->tokener;
		struct json_object * const object = json_tokener_parse_ex(tokener, data, count);

		if (object == NULL) {
			const enum json_tokener_error jerr = json_tokener_get_error(tokener);

			if (jerr != json_tokener_continue) {
				const enum json_tokener_error jerr = json_tokener_get_error(tokener);
				warnx("Unable to parse JSON: %s", json_tokener_error_desc(jerr));
				return json_tokener_get_parse_end(tokener) - 1;
			}
		} else {
			context->object = object;
		}
	}

	return count;
}

static struct json_object *
fetch_and_decode_json(const char *url) {
	struct fetch_and_decode_json context = {
		.tokener = json_tokener_new(),
		.object = NULL,
	};

	CURL * const easy = curl_easy_init();

	curl_easy_setopt(easy, CURLOPT_URL, url);
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, fetch_and_decode_json_write);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &context);

	const CURLcode res = curl_easy_perform(easy);
	if (res != CURLE_OK) {
		errx(EXIT_FAILURE, "curl_easy_perform %s: %s", url, curl_easy_strerror(res));
	}

	curl_easy_cleanup(easy);

	json_tokener_free(context.tokener);

	return context.object;
}

static struct {
	struct json_object *object;
} manifest;

static void
manifest_resolve_version(const char *version, const char **typep, const char **idp) {
	static const char * const types[] = {
		"release",
		"snapshot",
		"old_beta",
		"old_alpha",
	};
	const char * const separator = strchr(version, '/'), *type, *id;

	if (separator != NULL) {
		unsigned int i = 0;
		size_t length;

		while (i < sizeof (types) / sizeof (*types)
			&& (length = separator - version, strlen(types[i]) != length
				|| strncmp(types[i], version, length) != 0)) {
			i++;
		}

		if (i == sizeof (types) / sizeof (*types)) {
			errx(EXIT_FAILURE, "Unknown version type '%.*s'", (int)length, version);
		}

		type = types[i];
		id = separator + 1;
	} else {
		type = types[0];
		id = version;
	}

	if (strcmp("latest", id) == 0) {
		struct json_object *latest;

		if (!json_object_object_get_ex(manifest.object, "latest", &latest)) {
			errx(EXIT_FAILURE, "Unable to get 'latest' in version manifest!");
		}

		if (!json_object_object_get_ex(latest, type, &latest)) {
			errx(EXIT_FAILURE, "Unable to get 'latest.%s' in version manifest!", type);
		}

		const char * const resolved = json_object_get_string(latest);
		if (resolved == NULL) {
			errx(EXIT_FAILURE, "'latest.%s' in version manifest is null!", type);
		}

		id = resolved;
	}

	*typep = type;
	*idp = id;
}

static const char *
manifest_version_package_url(const char *type, const char *id) {
	struct json_object *versions_object;

	if (!json_object_object_get_ex(manifest.object, "versions", &versions_object)) {
		errx(EXIT_FAILURE, "Unable to get 'versions' in version manifest!");
	}

	if (!json_object_is_type(versions_object, json_type_array)) {
		errx(EXIT_FAILURE, "'versions' is not an array in version manifest!");
	}

	/* Must iterate all versions and find by type/id. */
	const size_t versions_object_length = json_object_array_length(versions_object);
	for (size_t idx = 0; idx < versions_object_length; idx++) {
		struct json_object * const version_object = json_object_array_get_idx(versions_object, idx);
		struct json_object *object;

		/* Check type. */
		if (!json_object_object_get_ex(version_object, "type", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'versions[%lu].type' in manifest!", idx);
		}

		const char * const version_object_type = json_object_get_string(object);
		if (version_object_type == NULL) {
			errx(EXIT_FAILURE, "'versions[%lu].type' is null in manifest!", idx);
		}

		if (strcmp(type, version_object_type) != 0) {
			continue;
		}

		/* Check id. */
		if (!json_object_object_get_ex(version_object, "id", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'versions[%lu].id' in manifest!", idx);
		}

		const char * const version_object_id = json_object_get_string(object);
		if (version_object_id == NULL) {
			errx(EXIT_FAILURE, "'versions[%lu].id' is null in manifest!", idx);
		}

		if (strcmp(id, version_object_id) != 0) {
			continue;
		}

		/* We found it, now get url */
		if (!json_object_object_get_ex(version_object, "url", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'versions[%lu].id' in manifest!", idx);
		}

		const char * const version_object_url = json_object_get_string(object);
		if (version_object_url == NULL) {
			errx(EXIT_FAILURE, "'versions[%lu].url' is null in manifest!", idx);
		}

		return version_object_url;
	}

	errx(EXIT_FAILURE, "Version %s/%s not found in manifest!", type, id);
}

void
manifest_setup(const char *url, time_t max_age) {
	char * const path = storage_version_manifest_path();
	bool update = false;
	struct stat st;

	if (stat(path, &st) != 0) {
		/* Either there is no manifest or an error occured. */
		if (errno != ENOENT) {
			err(EXIT_FAILURE, "stat '%s'", path);
		}
		update = true;
	} else if (time(NULL) - st.st_mtime >= max_age) {
		/* Max age expired, download new version. */
		update = true;
	}

	/* Download version manifest if required. */
	if (update) {
		storage_fetch(path, url);
	}

	/* Read version manifest from storage. */
	struct json_tokener * const tokener = json_tokener_new();
	const int fd = open(path, O_RDONLY);
	char buffer[getpagesize()];
	ssize_t copied;

	if (fd < 0) {
		err(EXIT_FAILURE, "open '%s'", path);
	}

	while (copied = read(fd, buffer, sizeof (buffer)), manifest.object == NULL && copied > 0) {
		manifest.object = json_tokener_parse_ex(tokener, buffer, copied);

		const enum json_tokener_error jerr = json_tokener_get_error(tokener);
		if (jerr != json_tokener_continue && jerr != json_tokener_success) {
			errx(EXIT_FAILURE, "Unable to parse version manifest file '%s': %s", path, json_tokener_error_desc(jerr));
		}
	}

	close(fd);
	json_tokener_free(tokener);

	free(path);

	/* NB: manifest.object will leak, missing a json_object_put. */
}

void
manifest_install_version(const char *version, char **pathp) {
	const char *type, *id;

	manifest_resolve_version(version, &type, &id);

	char * const path = storage_archive_path(id);
	if (access(path, R_OK) != 0) {
		const char * const package_url = manifest_version_package_url(type, id);
		struct json_object * const package_object = fetch_and_decode_json(package_url),
			*server_object, *object;

		if (!json_object_object_get_ex(package_object, "downloads", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'downloads' in package!");
		}

		if (!json_object_object_get_ex(object, "server", &server_object)) {
			errx(EXIT_FAILURE, "Unable to get 'downloads.server' in package!");
		}

		/* Get server archive's url. */
		if (!json_object_object_get_ex(server_object, "url", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'downloads.server.url' in package!");
		}

		const char * const server_object_url = json_object_get_string(object);
		if (server_object_url == NULL) {
			errx(EXIT_FAILURE, "'downloads.server.url' is null in package!");
		}

		/* Get server archive's sha1. */
		if (!json_object_object_get_ex(server_object, "sha1", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'downloads.server.sha1' in package!");
		}

		const char * const server_object_sha1 = json_object_get_string(object);
		if (server_object_sha1 == NULL) {
			errx(EXIT_FAILURE, "'downloads.server.sha1' is null in package!");
		}

		/* Get server archive's size. */
		if (!json_object_object_get_ex(server_object, "size", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'downloads.server.size' in package!");
		}

		errno = 0;
		const size_t server_object_size = json_object_get_uint64(object);
		if (server_object_size == 0 && errno != 0) {
			err(EXIT_FAILURE, "'downloads.server.size' is invalid in package!");
		}

		storage_fetch_and_verify(path, server_object_url,
			server_object_sha1, server_object_size);

		/* Release package object. */
		json_object_put(package_object);
	}

	if (pathp != NULL) {
		*pathp = path;
	} else {
		free(path);
	}
}
