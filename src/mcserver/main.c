#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <err.h>

#include "storage.h"
#include "manifest.h"

/* 48h default time to use for cache */
#define MCSERVER_DEFAULT_TTU 172800
#define MCSERVER_DEFAULT_KEEP 10
#define MCSERVER_DEFAULT_VERSION_MANIFEST_URL "https://launchermeta.mojang.com/mc/game/version_manifest.json"

struct mcserver_args {
	const char *data, *manifesturl;
	time_t ttu; /* Time to use, before updating local version manifest */

	const char *jvm;
	const char *world;

	const char *id;
	enum version version;
};

struct mcserver_command {
	const char * const name;
	void (* const run)(const struct mcserver_args *);
};

static const struct option longopts[] = {
	{ "snapshot", required_argument },
	{ "release",  required_argument },
	{ "beta",     required_argument },
	{ "alpha",    required_argument },

	{ "manifest-url", required_argument },
	{ "jvm", required_argument },
	{ "world", required_argument },

	{ "help", no_argument },
	{ "nocache", no_argument },

	{ },
};

static void
mcserver_version(const struct mcserver_args *args) {
	puts(manifest_resolve(args->version, args->id));
}

static void
mcserver_install(const struct mcserver_args *args) {
	const enum version version = args->version;
	const char * const id = manifest_resolve(version, args->id);
	struct server_archive archive;

	manifest_server_archive_fetch(version, id, &archive);

	storage_download_server_archive(version, id, &archive);

	manifest_server_archive_cleanup(&archive);
}

static void
mcserver_launch(const struct mcserver_args *args) {
	const enum version version = args->version;
	const char * const id = manifest_resolve(version, args->id);
	const char * const path = storage_server_archive_path(version, id);
	char * const arguments[] = {
		(char *)args->jvm,
		"-Xmx1024M", "-Xms1024M",
		"-jar", (char *)path,
		NULL
	};

	if (args->world == NULL) {
		errx(EXIT_FAILURE, "You must specifiy a world directory when launching a server!");
	}

	if (chdir(args->world) != 0) {
		err(EXIT_FAILURE, "chdir %s", args->world);
	}

	execvp(args->jvm, arguments);
	err(EXIT_FAILURE, "execvp %s (%s)", args->jvm, path);
}

static const struct mcserver_command commands[] = {
	{ "version", mcserver_version },
	{ "install", mcserver_install },
	{ "launch",  mcserver_launch },
};

static void
mcserver_usage(const char *mcservername) {
	fprintf(stderr, "usage: %1$s [-cache <duration>] [-manifest-url <url>] [-snapshot <id> | -release <id> | -beta <id> | -alpha <id>] install\n"
		        "       %1$s [-jvm <java>] [-snapshot <id> | -release <id> | -beta <id> | -alpha <id>] launch\n"
		        "       %1$s -help\n",
		mcservername);
	exit(EXIT_FAILURE);
}

static struct mcserver_args
mcserver_parse_args(int argc, char **argv) {
	struct mcserver_args args = {
		.data = NULL,
		.manifesturl = MCSERVER_DEFAULT_VERSION_MANIFEST_URL,
		.ttu = MCSERVER_DEFAULT_TTU,

		.version = VERSION_RELEASE,
		.id = "latest",

		.jvm = "java",
		.world = NULL,
	};
	int longindex, c;

	while (c = getopt_long_only(argc, argv, ":", longopts, &longindex), c != -1) {
		switch (c) {
		case 0:
			switch (longindex) {
			case 0:
				args.version = VERSION_SNAPSHOT;
				args.id = optarg;
				break;
			case 1:
				args.version = VERSION_RELEASE;
				args.id = optarg;
				break;
			case 2:
				args.version = VERSION_BETA;
				args.id = optarg;
				break;
			case 3:
				args.version = VERSION_ALPHA;
				args.id = optarg;
				break;
			case 4: args.manifesturl = optarg; break;
			case 5: args.jvm = optarg; break;
			case 6: args.world = optarg; break;
			case 7:
				mcserver_usage(*argv);
			case 8:
				args.ttu = 0;
				break;
			}
			break;
		case '?':
			fprintf(stderr, "%s: Invalid option %s\n", *argv, argv[optind - 1]);
			mcserver_usage(*argv);
		case ':':
			fprintf(stderr, "%s: Missing option argument after -%s\n", *argv, longopts[longindex].name);
			mcserver_usage(*argv);
		}
	}

	if (argc - optind != 1) {
		mcserver_usage(*argv);
	}

	return args;
}

int
main(int argc, char **argv) {
	const struct mcserver_args args = mcserver_parse_args(argc, argv);
	const struct mcserver_command *current = commands,
		* const commandsend = commands + sizeof (commands) / sizeof (*commands);
	const char * const commandname = argv[optind], **argpos;

	while (current != commandsend && strcmp(commandname, current->name) != 0) {
		current++;
	}

	if (current == commandsend) {
		fprintf(stderr, "%s: Invalid command %s\n", *argv, commandname);
		mcserver_usage(*argv);
	}

	storage_init(args.data);
	manifest_init(args.manifesturl, args.ttu);

	current->run(&args);

	return EXIT_SUCCESS;
}
