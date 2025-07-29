#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>

#include <curl/curl.h>
#include <openssl/evp.h>

/* The terminating slash is important for path compositions */
#ifdef __APPLE__
#define STORAGE_DATA_DIR  "Library/Application Support/mcserver/"
#else
#define STORAGE_DATA_DIR  ".local/share/mcserver/"
#endif

#define STORAGE_DATA_VERSION_MANIFEST_FILE "version_manifest.json"
#define STORAGE_DATA_SERVER_ARCHIVES_DIR "server-archives/"
#define STORAGE_SERVER_ARCHIVE_EXTENSION ".jar"

#define STORAGE_SERVER_ARCHIVE_PATH_SIZE(idlen) (storage.pathlen \
	+ sizeof (STORAGE_DATA_SERVER_ARCHIVES_DIR) + idlen + sizeof (STORAGE_SERVER_ARCHIVE_EXTENSION) - 1)

struct archive_entry {
	char *name;
	time_t mtime;
};

static struct {
	char *path;
	size_t pathlen;

	char *transient;
	size_t transientlen;
} storage;

static void
storage_deinit(void) {
	free(storage.path);
	free(storage.transient);
}

void
storage_init(const char *dir) {
	const char *home = getenv("HOME");

	if (home == NULL || *home != '/') {
		errx(EXIT_FAILURE, "Unable to get home directory");
	}

	const size_t homelen = strlen(home);

	if (dir == NULL) {
		static const char userdata[] = STORAGE_DATA_DIR;
		char path[homelen + sizeof (userdata) + 1];

		strncpy(path, home, homelen);

		size_t position = homelen;
		if (path[position - 1] != '/') {
			path[position] = '/';
			position++;
		}

		strncpy(path + position, userdata, sizeof (userdata));

		storage.path = strdup(path);
	} else {
		storage.path = strdup(dir);
	}

	if (mkdir(storage.path, 0777) != 0 && errno != EEXIST) {
		err(EXIT_FAILURE, "mkdir %s", storage.path);
	}

	storage.pathlen = strlen(storage.path);

	char archivespath[storage.pathlen + sizeof (STORAGE_DATA_SERVER_ARCHIVES_DIR)];
	*stpncpy(stpncpy(archivespath,
		storage.path, storage.pathlen),
		STORAGE_DATA_SERVER_ARCHIVES_DIR, sizeof (STORAGE_DATA_SERVER_ARCHIVES_DIR) - 1)
	= '\0';

	if (mkdir(archivespath, 0777) != 0 && errno != EEXIST) {
		err(EXIT_FAILURE, "mkdir %s", archivespath);
	}

	storage.transient = NULL;
	storage.transientlen = 0;

	atexit(storage_deinit);
}

static const char *
storage_transient_set(const char *string, size_t stringlen) {

	if (stringlen > storage.transientlen) {
		char *newtransient = realloc(storage.transient, stringlen + 1);
		if (newtransient == NULL) {
			err(EXIT_FAILURE, "realloc");
		}
		storage.transient = newtransient;
		storage.transientlen = stringlen;
	}

	return strncpy(storage.transient, string, storage.transientlen + 1);
}

const char *
storage_version_manifest_path(void) {
	char path[storage.pathlen + sizeof (STORAGE_DATA_VERSION_MANIFEST_FILE)];

	*stpncpy(stpncpy(path,
		storage.path, storage.pathlen),
		STORAGE_DATA_VERSION_MANIFEST_FILE, sizeof (STORAGE_DATA_VERSION_MANIFEST_FILE) - 1)
	= '\0';

	return storage_transient_set(path, sizeof (path) - 1);
}

static void
storage_server_archive_path_init(char *path, const char *id, size_t idlen) {
	*stpncpy(stpncpy(stpncpy(stpncpy(path,
		storage.path, storage.pathlen),
		STORAGE_DATA_SERVER_ARCHIVES_DIR, sizeof (STORAGE_DATA_SERVER_ARCHIVES_DIR) - 1),
		id, idlen),
		STORAGE_SERVER_ARCHIVE_EXTENSION, sizeof (STORAGE_SERVER_ARCHIVE_EXTENSION) - 1)
	= '\0';
}


const char *
storage_server_archive_path(enum version version, const char *id) {
	const size_t idlen = strlen(id);
	char path[STORAGE_SERVER_ARCHIVE_PATH_SIZE(idlen)];

	storage_server_archive_path_init(path, id, idlen);

	return storage_transient_set(path, sizeof (path) - 1);
}

static int
storage_download_progress(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	char progress[] = " [                               ]\r";
	size_t filled = 0;

	if (dltotal > 0) {
		filled = (float)dlnow / dltotal * (sizeof (progress) - 5);
	}

	memset(progress + 2, '-', filled);

	fputs(clientp, stdout);
	fputs(progress, stdout);
	fflush(stdout);

	return 0;
}

static bool
storage_check_server_archive(const char *path, const uint8_t *ardigest, size_t ardigestsz, size_t arsize) {
	EVP_MD_CTX * const ctx = EVP_MD_CTX_new();
	int fd = open(path, O_RDONLY);
	bool success = false;

	do {
		char buffer[getpagesize()];
		ssize_t readval;

		if(fd < 0) {
			warn("open %s", path);
			break;
		}

		EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);

		while(readval = read(fd, buffer, sizeof(buffer)), readval > 0) {
			EVP_DigestUpdate(ctx, buffer, readval);
			arsize -= readval;
		}

		if(readval != 0) {
			warn("read %s", path);
			break;
		}

		uint8_t digest[EVP_MAX_MD_SIZE];
		unsigned int digestsz;

		EVP_DigestFinal_ex(ctx, digest, &digestsz);

		if(ardigestsz != digestsz || memcmp(digest, ardigest, digestsz) != 0) {
			warnx("Incoherent digest for downloaded archive!");
			break;
		}

		if(arsize != 0) {
			warnx("Incoherent size for downloaded archive!");
			break;
		}

		success = true;
	} while (0);

	close(fd);
	EVP_MD_CTX_free(ctx);

	return success;
}

void
storage_download_server_archive(enum version version, const char *id, const struct server_archive *archive) {
	const size_t idlen = strlen(id);
	char path[STORAGE_SERVER_ARCHIVE_PATH_SIZE(idlen)];

	storage_server_archive_path_init(path, id, idlen);

	FILE *filep = fopen(path, "w");
	CURL *easy = curl_easy_init();

	if (filep == NULL) {
		err(EXIT_FAILURE, "fopen %s", path);
	}

	curl_easy_setopt(easy, CURLOPT_URL, archive->url);
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, filep);

	if (!!isatty(STDOUT_FILENO)) {
		curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, storage_download_progress);
		curl_easy_setopt(easy, CURLOPT_XFERINFODATA, id);
		curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
	}

	const CURLcode res = curl_easy_perform(easy);
	if (res != CURLE_OK) {
		errx(EXIT_FAILURE, "curl_easy_perform %s: %s", archive->url, curl_easy_strerror(res));
	}

	curl_easy_cleanup(easy);
	fclose(filep);

	if (!storage_check_server_archive(path, archive->sha1, sizeof (archive->sha1), archive->size)) {
		unlink(path);
		errx(EXIT_FAILURE, "Unable to check validity of archive for version %s", id);
	}
}
