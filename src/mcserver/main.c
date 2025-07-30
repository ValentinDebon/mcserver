#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <err.h>

#include "config.h"
#include "storage.h"
#include "manifest.h"

enum mcserver_option {
	MCSERVER_OPTION_SNAPSHOT,
	MCSERVER_OPTION_RELEASE,
	MCSERVER_OPTION_BETA,
	MCSERVER_OPTION_ALPHA,
	MCSERVER_OPTION_JVM,
	MCSERVER_OPTION_WORLD,
	MCSERVER_OPTION_HELP,
	MCSERVER_OPTION_NOCACHE,
};

struct mcserver_args {
	const char *data;
	time_t max_age;

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
	[MCSERVER_OPTION_SNAPSHOT] = { "snapshot", required_argument },
	[MCSERVER_OPTION_RELEASE]  = { "release", required_argument },
	[MCSERVER_OPTION_BETA]     = { "beta", required_argument },
	[MCSERVER_OPTION_ALPHA]    = { "alpha", required_argument },
	[MCSERVER_OPTION_JVM]      = { "jvm", required_argument },
	[MCSERVER_OPTION_WORLD]    = { "world", required_argument },
	[MCSERVER_OPTION_HELP]     = { "help", no_argument },
	[MCSERVER_OPTION_NOCACHE]  = { "nocache", no_argument },
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
	fprintf(stderr, "usage: %1$s [-snapshot <id>] [-release <id>] [-beta <id>] [-alpha <id>]"
					" [-jvm <path] [-world <path>] [-nocache] install|launch|version\n"
		        "       %1$s -help\n",
		mcservername);
	exit(EXIT_FAILURE);
}

static struct mcserver_args
mcserver_parse_args(int argc, char **argv) {
	struct mcserver_args args = {
		.data = NULL,
		.max_age = CONFIG_VERSION_MANIFEST_MAX_AGE,

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
			case MCSERVER_OPTION_SNAPSHOT:
				args.version = VERSION_SNAPSHOT;
				args.id = optarg;
				break;
			case MCSERVER_OPTION_RELEASE:
				args.version = VERSION_RELEASE;
				args.id = optarg;
				break;
			case MCSERVER_OPTION_BETA:
				args.version = VERSION_BETA;
				args.id = optarg;
				break;
			case MCSERVER_OPTION_ALPHA:
				args.version = VERSION_ALPHA;
				args.id = optarg;
				break;
			case MCSERVER_OPTION_JVM: args.jvm = optarg; break;
			case MCSERVER_OPTION_WORLD: args.world = optarg; break;
			case MCSERVER_OPTION_HELP:
				mcserver_usage(*argv);
			case MCSERVER_OPTION_NOCACHE:
				args.max_age = 0;
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
	manifest_init(CONFIG_VERSION_MANIFEST_URL, args.max_age);

	current->run(&args);

	return EXIT_SUCCESS;
}
