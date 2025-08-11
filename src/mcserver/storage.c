#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>

#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <curl/curl.h>
#include <openssl/evp.h>

/* The terminating slashes are important for path compositions. */
#ifdef __APPLE__
#define STORAGE_DATA_DIR "Library/Application Support/mcserver/"
#else
#define STORAGE_DATA_DIR ".local/share/mcserver/"
#endif

#define STORAGE_DATA_VERSION_MANIFEST_FILE "version_manifest.json"
#define STORAGE_DATA_ARCHIVES_DIR "archives/"
#define STORAGE_DATA_WORLDS_DIR "worlds/"

static struct {
	char *path;
	struct winsize ws;
} storage;

static void __attribute__((constructor))
storage_setup(void) {
	const char * const home = getenv("HOME");

	if (home == NULL || *home != '/') {
		errx(EXIT_FAILURE, "Degenerate $HOME");
	}

	if (asprintf(&storage.path, "%s/" STORAGE_DATA_DIR, home) < 0) {
		errx(EXIT_FAILURE, "asprintf");
	}

	if (mkdir(storage.path, 0777) != 0 && errno != EEXIST) {
		err(EXIT_FAILURE, "mkdir '%s'", storage.path);
	}

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &storage.ws) != 0) {
		storage.ws.ws_col = 0;
	}

	/* NB: storage.path leaks, missing a free. */
}

char *
storage_version_manifest_path(void) {
	char *path;

	if (asprintf(&path, "%s" STORAGE_DATA_VERSION_MANIFEST_FILE, storage.path) < 0) {
		errx(EXIT_FAILURE, "asprintf");
	}

	return path;
}

char *
storage_archive_path(const char *id) {
	char *path;

	if (*id == '\0' || *id == '.'
		|| strchr(id, '/') != NULL) {
		errx(EXIT_FAILURE, "Invalid id '%s'", id);
	}

	if (asprintf(&path, "%s" STORAGE_DATA_ARCHIVES_DIR "%s.jar", storage.path, id) < 0) {
		errx(EXIT_FAILURE, "asprintf");
	}

	char * const separator = strrchr(path, '/');
	*separator = '\0';
	if (mkdir(path, 0777) != 0 && errno != EEXIST) {
		err(EXIT_FAILURE, "mkdir '%s'", path);
	}
	*separator = '/';

	return path;
}

char *
storage_world_directory(const char *world) {
	char *path;

	if (*world == '\0' || *world == '.'
		|| strchr(world, '/') != NULL) {
		errx(EXIT_FAILURE, "Invalid world '%s'", world);
	}

	if (asprintf(&path, "%s" STORAGE_DATA_WORLDS_DIR "%s", storage.path, world) < 0) {
		errx(EXIT_FAILURE, "asprintf");
	}

	char * const separator = strrchr(path, '/');
	*separator = '\0';
	if (mkdir(path, 0777) != 0 && errno != EEXIST) {
		err(EXIT_FAILURE, "mkdir '%s'", path);
	}
	*separator = '/';

	if (mkdir(path, 0777) != 0 && errno != EEXIST) {
		err(EXIT_FAILURE, "mkdir '%s'", path);
	}

	return path;
}

void
storage_fetch(const char *path, const char *url) {
	FILE * const filep = fopen(path, "w");
	CURL * const easy = curl_easy_init();

	if (filep == NULL) {
		err(EXIT_FAILURE, "fopen '%s'", path);
	}

	curl_easy_setopt(easy, CURLOPT_URL, url);
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, filep);

	const CURLcode res = curl_easy_perform(easy);
	if (res != CURLE_OK) {
		unlink(path);
		errx(EXIT_FAILURE, "curl_easy_perform '%s': %s", url, curl_easy_strerror(res));
	}

	curl_easy_cleanup(easy);
	fclose(filep);
}

static inline uint8_t 
hex2nibble(uint8_t hex) {
       return isdigit(hex) ? hex - '0' : hex - 'a' + 10;
}

static int
storage_fetch_progress_interactive(void *clientp,
	curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	char progress[storage.ws.ws_col];
	char *cursor = progress;

	cursor = stpcpy(cursor, clientp);
	*cursor++ = ' ';
	*cursor++ = '[';

	const size_t gap = progress + sizeof (progress) - cursor - 2;
	size_t filled = 0;

	if (dltotal != 0) {
		filled = dlnow * gap / dltotal;
	}

	memset(cursor, '=', filled);
	cursor += filled;

	memset(cursor, ' ', gap - filled);
	cursor += gap - filled;

	*cursor++ = ']';
	*cursor++ = '\r';

	fwrite(progress, sizeof (*progress), sizeof (progress), stdout);
	fflush(stdout);

	return 0;
}

void
storage_fetch_and_verify(const char *path, const char *url, const char *sha1, size_t expected_size) {
	uint8_t expected_digest[20];

	/* Hexadecimal string, two nibbles per byte. */
	if (strlen(sha1) != 2 * sizeof (expected_digest)) {
		errx(EXIT_FAILURE, "Invalid SHA1 digest length for '%s'", sha1);
	}

	for (unsigned i = 0; i < sizeof (expected_digest); i++) {
		const char high = sha1[2 * i], low = sha1[2 * i + 1];

		if (!isxdigit(high) || !isxdigit(low)) {
			errx(EXIT_FAILURE, "Invalid SHA1 digest '%s', expected a hexadecimal string", sha1);
		}

		expected_digest[i] = hex2nibble(high) << 4 | hex2nibble(low);
	}

	/* Download server archive. */
	FILE * const filep = fopen(path, "w");
	CURL * const easy = curl_easy_init();

	if (filep == NULL) {
		err(EXIT_FAILURE, "fopen '%s'", path);
	}

	curl_easy_setopt(easy, CURLOPT_URL, url);
	curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, filep);

	char linefeed;
	const char * const name = strrchr(path, '/') + 1;
	if (strlen(name) + 4 <= storage.ws.ws_col) { /* 4 == strlen(" []\r") */
		/* Enable interactive progress if the name is not too long. */
		curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, storage_fetch_progress_interactive);
		curl_easy_setopt(easy, CURLOPT_XFERINFODATA, name);
		linefeed = '\n';
	} else {
		linefeed = '\0';
	}
	curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);

	const CURLcode res = curl_easy_perform(easy);

	if (linefeed) {
		fputc(linefeed, stdout);
	}

	if (res != CURLE_OK) {
		unlink(path);
		errx(EXIT_FAILURE, "curl_easy_perform '%s': %s", url, curl_easy_strerror(res));
	}

	curl_easy_cleanup(easy);
	fclose(filep);

	/* Verify server archive signature. */
	EVP_MD_CTX * const ctx = EVP_MD_CTX_new();
	const int fd = open(path, O_RDONLY);
	bool valid = false;

	do {
		char buffer[getpagesize()];
		ssize_t copied;

		if (fd < 0) {
			warn("open %s", path);
			break;
		}

		EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);

		while (copied = read(fd, buffer, sizeof (buffer)), copied > 0) {
			EVP_DigestUpdate(ctx, buffer, copied);
			expected_size -= copied;
		}

		if (copied != 0) {
			warn("read %s", path);
			break;
		}

		uint8_t digest[EVP_MAX_MD_SIZE];
		unsigned int digestsz;

		EVP_DigestFinal_ex(ctx, digest, &digestsz);

		if (sizeof (expected_digest) != digestsz
			|| memcmp(digest, expected_digest, digestsz) != 0) {
			warnx("Incoherent digest for downloaded archive!");
			break;
		}

		if (expected_size != 0) {
			warnx("Incoherent size for downloaded archive!");
			break;
		}

		valid = true;
	} while (0);

	close(fd);
	EVP_MD_CTX_free(ctx);

	if (!valid) {
		unlink(path);
		exit(EXIT_FAILURE);
	}

	/* Make the archive read only. */
	if (chmod(path, 0444) != 0) {
		warn("chmod '%s'", path);
	}
}
