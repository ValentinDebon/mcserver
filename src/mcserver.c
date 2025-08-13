/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <err.h>

#include <stdbool.h>
#include <stdnoreturn.h>

#include "config.h"
#include "manifest.h"
#include "storage.h"

enum mcserver_option {
	MCSERVER_OPTION_VERSION,
	MCSERVER_OPTION_WORLD,
	MCSERVER_OPTION_JVM,
	MCSERVER_OPTION_NOUPDATE,
	MCSERVER_OPTION_NOCACHE,
	MCSERVER_OPTION_HELP,
};

enum mcserver_synopsis {
	MCSERVER_SYNOPSIS_LAUNCH,
	MCSERVER_SYNOPSIS_INSTALL,
};

struct mcserver_args {
	char *version;
	char *world;
	char *jvm;

	time_t max_age;

	enum mcserver_synopsis synopsis;
};

static const struct option longopts[] = {
	[MCSERVER_OPTION_VERSION]  = { "version", required_argument },
	[MCSERVER_OPTION_WORLD]    = { "world", required_argument },
	[MCSERVER_OPTION_JVM]      = { "jvm", required_argument },
	[MCSERVER_OPTION_NOUPDATE] = { "noupdate", no_argument },
	[MCSERVER_OPTION_NOCACHE]  = { "nocache", no_argument },
	[MCSERVER_OPTION_HELP]     = { "help", no_argument },
	{ },
};

static const char * const synopses_names[] = {
	[MCSERVER_SYNOPSIS_LAUNCH]  = "launch",
	[MCSERVER_SYNOPSIS_INSTALL] = "install",
};

static noreturn void
mcserver_launch(const struct mcserver_args *args, int argc, char **argv) {
	char *path;

	manifest_install_version(args->version, &path);

	char ** const xargv = malloc((6 + argc - optind) * sizeof (*xargv));
	unsigned int i = 0;

	xargv[i++] = args->jvm;
	xargv[i++] = "-Xmx1024M";
	xargv[i++] = "-Xms1024M";

	while (optind < argc) {
		xargv[i++] = argv[optind++];
	}

	xargv[i++] = "-jar";
	xargv[i++] = path;
	xargv[i] = NULL;

	const char * const workdir = storage_world_directory(args->world);
	if (chdir(workdir) != 0) {
		err(EXIT_FAILURE, "chdir '%s'", workdir);
	}

	execvp(args->jvm, xargv);

	err(EXIT_FAILURE, "execvp %s (-jar %s)", args->jvm, path);
}

static noreturn void
mcserver_install(const struct mcserver_args *args) {

	manifest_install_version(args->version, NULL);

	exit(EXIT_SUCCESS);
}

static noreturn void
mcserver_usage(const char *name, int status) {
	fprintf(stderr, "usage: %1$s [-version <version>] [-world <name>] [-jvm <path>] [-noupdate] [-nocache] launch ...\n"
	                "       %1$s [-version <version>] [-noupdate] [-nocache] install\n"
	                "       %1$s -help\n", name);
	exit(status);
}

static struct mcserver_args
mcserver_parse_args(int argc, char **argv) {
	struct mcserver_args args = {
		.max_age = CONFIG_VERSION_MANIFEST_MAX_AGE,
	};
	bool noupdate = false, nocache = false, help = false;
	int longindex, c;

	while ((c = getopt_long_only(argc, argv, ":", longopts, &longindex)) != -1) {
		switch (c) {
		case 0:
			switch (longindex) {
			case MCSERVER_OPTION_VERSION:
				args.version = optarg;
				break;
			case MCSERVER_OPTION_WORLD:
				args.world = optarg;
				break;
			case MCSERVER_OPTION_JVM:
				args.jvm = optarg;
				break;
			case MCSERVER_OPTION_NOUPDATE:
				noupdate = true;
				break;
			case MCSERVER_OPTION_NOCACHE:
				nocache = true;
				break;
			case MCSERVER_OPTION_HELP:
				help = true;
				break;
			}
			break;
		case '?':
			fprintf(stderr, "%s: Invalid option %s\n", *argv, argv[optind - 1]);
			mcserver_usage(*argv, EXIT_FAILURE);
		case ':':
			fprintf(stderr, "%s: Missing option argument after -%s\n", *argv, longopts[longindex].name);
			mcserver_usage(*argv, EXIT_FAILURE);
		}
	}

	if (help) {
		mcserver_usage(*argv, EXIT_SUCCESS);
	}

	if (argc - optind == 0) {
		mcserver_usage(*argv, EXIT_FAILURE);
	}

	const unsigned int synopses_count = sizeof (synopses_names) / sizeof (*synopses_names);
	const char * const synopsis_name = argv[optind];

	while (args.synopsis < synopses_count
		&& strcmp(synopses_names[args.synopsis], synopsis_name) != 0) {
		args.synopsis++;
	}

	if (args.synopsis == synopses_count) {
		fprintf(stderr, "%s: Invalid synopsis '%s'\n", *argv, synopsis_name);
		mcserver_usage(*argv, EXIT_FAILURE);
	}
	optind++;

	if (noupdate && nocache) {
		fprintf(stderr, "%s: Options noupdate and nocache together are nonsensical\n", *argv);
		mcserver_usage(*argv, EXIT_FAILURE);
	}

	if (args.version == NULL) {
		args.version = "latest";
	}

	if (args.synopsis == MCSERVER_SYNOPSIS_LAUNCH) {
		if (args.world == NULL) {
			const size_t worldsz = HOST_NAME_MAX + 1;
			char * const world = malloc(worldsz);

			if (gethostname(world, worldsz) != 0) {
				err(EXIT_FAILURE, "gethostname");
			}
			world[HOST_NAME_MAX] = '\0';

			args.world = world;
			/* NB: Will leak, missing free. */
		}

		if (args.jvm == NULL) {
			args.jvm = "java";
		}
	} else if (args.world != NULL || args.jvm != NULL) {
		fprintf(stderr, "%s: Options world and jvm can only be used for launch\n", *argv);
		mcserver_usage(*argv, EXIT_FAILURE);
	}

	if (nocache) {
		args.max_age = 0;
	} else if (noupdate) {
		args.max_age = _Generic ((time_t)0,
			int: INT_MAX, long: LONG_MAX,
			long long: LLONG_MAX);
	}

	return args;
}

int
main(int argc, char *argv[]) {
	const struct mcserver_args args = mcserver_parse_args(argc, argv);

	manifest_setup(CONFIG_VERSION_MANIFEST_URL, args.max_age);

	switch (args.synopsis) {
	case MCSERVER_SYNOPSIS_LAUNCH:
		mcserver_launch(&args, argc, argv);
	case MCSERVER_SYNOPSIS_INSTALL:
		mcserver_install(&args);
	}
}
