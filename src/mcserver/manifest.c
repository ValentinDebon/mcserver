#include "manifest.h"

#include "storage.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>

#include <json-c/json.h>
#include <curl/curl.h>

struct download_json {
	struct json_tokener *tokener;
	struct json_object *object;
};

static struct {
	struct json_object *object;
} manifest;

static const char * const versiontypes[] = {
	"snapshot",
	"release",
	"old_beta",
	"old_alpha",
};

static void
manifest_deinit(void) {
	json_object_put(manifest.object);
}

void
manifest_init(const char *url, time_t ttu) {
	const char * const path = storage_version_manifest_path();
	bool update = false;
	struct stat st;

	if (stat(path, &st) != 0) {
		/* Either the file does not exist, or we can't acquire informations */
		if (errno != ENOENT) {
			err(EXIT_FAILURE, "stat %s", path);
		}
		update = true;
	} else if (time(NULL) - st.st_mtime >= ttu) {
		/* Time to use expired, download new version */
		update = true;
	}

	/* Download version manifest if required */
	if (update) {
		FILE *filep = fopen(path, "w");
		CURL *easy = curl_easy_init();

		if (filep == NULL) {
			err(EXIT_FAILURE, "fopen %s", path);
		}

		curl_easy_setopt(easy, CURLOPT_URL, url);
		curl_easy_setopt(easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
		curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, fwrite);
		curl_easy_setopt(easy, CURLOPT_WRITEDATA, filep);

		const CURLcode res = curl_easy_perform(easy);
		if (res != CURLE_OK) {
			errx(EXIT_FAILURE, "curl_easy_perform %s: %s", url, curl_easy_strerror(res));
		}

		curl_easy_cleanup(easy);
		fclose(filep);
	}

	/* Read version manifest from storage */
	struct json_tokener *tokener = json_tokener_new();
	int fd = open(path, O_RDONLY);
	char buffer[getpagesize()];
	ssize_t readval;

	if (fd < 0) {
		err(EXIT_FAILURE, "open %s", path);
	}

	while (readval = read(fd, buffer, sizeof (buffer)), manifest.object == NULL && readval > 0) {
		manifest.object = json_tokener_parse_ex(tokener, buffer, readval);

		const enum json_tokener_error jerr = json_tokener_get_error(tokener);
		if (jerr != json_tokener_continue && jerr != json_tokener_success) {
			errx(EXIT_FAILURE, "Unable to parse version manifest file %s: %s", path, json_tokener_error_desc(jerr));
		}
	}

	close(fd);
	json_tokener_free(tokener);

	atexit(manifest_deinit);
}

const char *
manifest_resolve(enum version version, const char *id) {
	const char * const typekey = versiontypes[version];

	/* Nothing to resolve, id is explicit */
	if (strcmp("latest", id) != 0) {
		return id;
	}

	struct json_object *latest;
	if (!json_object_object_get_ex(manifest.object, "latest", &latest)) {
		errx(EXIT_FAILURE, "Unable to get 'latest' in version manifest!");
	}
	if (!json_object_object_get_ex(latest, typekey, &latest)) {
		errx(EXIT_FAILURE, "Unable to get 'latest.%s' in version manifest!", typekey);
	}

	const char * const resolved = json_object_get_string(latest);
	if (resolved == NULL) {
		errx(EXIT_FAILURE, "'latest.%s' in version manifest is null!", typekey);
	}

	return resolved;
}

static size_t
download_json_write(const void *data, size_t one, size_t count, struct download_json *download) {

	/* Discard bytes after one object was parsed, abort if an error occured */
	if (download->object == NULL) {
		struct json_tokener * const tokener = download->tokener;
		struct json_object * const object = json_tokener_parse_ex(tokener, data, count);
	
		if (object == NULL) {
			const enum json_tokener_error jerr = json_tokener_get_error(tokener);

			if (jerr != json_tokener_continue) {
				const enum json_tokener_error jerr = json_tokener_get_error(tokener);
				warnx("Unable to parse JSON: %s", json_tokener_error_desc(jerr));
				return json_tokener_get_parse_end(tokener) - 1;
			}
		} else {
			download->object = object;
		}
	}

	return count;
}

static struct json_object *
download_json(const char *url) {
	struct download_json download = {
		.tokener = json_tokener_new(),
		.object = NULL,
	};

	CURL *easy = curl_easy_init();

	curl_easy_setopt(easy, CURLOPT_URL, url);
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, download_json_write);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &download);

	const CURLcode res = curl_easy_perform(easy);
	if (res != CURLE_OK) {
		errx(EXIT_FAILURE, "curl_easy_perform %s: %s", url, curl_easy_strerror(res));
	}

	curl_easy_cleanup(easy);

	json_tokener_free(download.tokener);

	return download.object;
}

static inline uint8_t 
hex2nibble(uint8_t hex) {
	return isdigit(hex) ? hex - '0' : hex - 'a' + 10;
}

void
manifest_server_archive_fetch(enum version version, const char *id, struct server_archive *archive) {
	const char * const type = versiontypes[version], *url = NULL;

	/****************************************************
	 * Find the version's package url from the manifest *
	 ****************************************************/
	struct json_object *versions;

	if (!json_object_object_get_ex(manifest.object, "versions", &versions)) {
		errx(EXIT_FAILURE, "Unable to get 'versions' in version manifest!");
	}

	if (!json_object_is_type(versions, json_type_array)) {
		errx(EXIT_FAILURE, "'versions' is not an array in version manifest!");
	}

	/* Must iterate all versions and find by type/id */
	const size_t versionslen = json_object_array_length(versions);
	for (size_t idx = 0; url == NULL && idx < versionslen; idx++) {
		struct json_object * const version = json_object_array_get_idx(versions, idx);
		struct json_object *object;

		/* Check type */
		if (!json_object_object_get_ex(version, "type", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'versions[%lu].type' in manifest!", idx);
		}

		const char * const vertype = json_object_get_string(object);

		if (vertype == NULL) {
			errx(EXIT_FAILURE, "'versions[%lu].type' is null in manifest!", idx);
		}

		if (strcmp(type, vertype) != 0) {
			continue;
		}

		/* Check id */
		if (!json_object_object_get_ex(version, "id", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'versions[%lu].id' in manifest!", idx);
		}

		const char * const verid = json_object_get_string(object);

		if (verid == NULL) {
			errx(EXIT_FAILURE, "'versions[%lu].id' is null in manifest!", idx);
		}

		if (strcmp(id, verid) != 0) {
			continue;
		}

		/* We found it, now get url */
		if (!json_object_object_get_ex(version, "url", &object)) {
			errx(EXIT_FAILURE, "Unable to get 'versions[%lu].id' in manifest!", idx);
		}

		const char * const verurl = json_object_get_string(object);

		if (verurl == NULL) {
			errx(EXIT_FAILURE, "'versions[%lu].url' is null in manifest!", idx);
		}

		url = verurl;
	}

	/**************************************************************************************************
	 * Now we have the package's url, download json object, and find download informations for server *
	 **************************************************************************************************/
	struct json_object * const package = download_json(url), *download, *object;

	if (!json_object_object_get_ex(package, "downloads", &download)) {
		errx(EXIT_FAILURE, "Unable to get 'downloads' in package!");
	}

	if (!json_object_object_get_ex(download, "server", &download)) {
		errx(EXIT_FAILURE, "Unable to get 'downloads.server' in package!");
	}

	/* Get download's url */
	if (!json_object_object_get_ex(download, "url", &object)) {
		errx(EXIT_FAILURE, "Unable to get 'downloads.server.url' in package!");
	}

	const char * const downloadurl = json_object_get_string(object);

	if (downloadurl == NULL) {
		errx(EXIT_FAILURE, "'downloads.server.url' is null in package!");
	}

	archive->url = strdup(downloadurl);

	/* Get download's sha1 */
	if (!json_object_object_get_ex(download, "sha1", &object)) {
		errx(EXIT_FAILURE, "Unable to get 'downloads.server.sha1' in package!");
	}

	const char * const downloadsha1 = json_object_get_string(object);

	if (downloadurl == NULL) {
		errx(EXIT_FAILURE, "'downloads.server.sha1' is null in package!");
	}

	for (unsigned i = 0; i < 20; i++) {
		archive->sha1[i] = hex2nibble(downloadsha1[2 * i]) << 4 | hex2nibble(downloadsha1[2 * i + 1]);
	}

	/* Get download's size */
	if (!json_object_object_get_ex(download, "size", &object)) {
		errx(EXIT_FAILURE, "Unable to get 'downloads.server.size' in package!");
	}

	errno = 0;
	const size_t downloadsize = json_object_get_uint64(object);

	if (downloadsize == 0 && errno != 0) {
		err(EXIT_FAILURE, "'downloads.server.size' is invalid in package!");
	}

	archive->size = downloadsize;

	/* Release package */
	json_object_put(package);
}

void
manifest_server_archive_cleanup(struct server_archive *archive) {
	free(archive->url);
}
