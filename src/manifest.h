/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef MANIFEST_H
#define MANIFEST_H

#include <time.h>

void manifest_setup(const char *url, time_t max_age);

void manifest_install_version(const char *version, char **pathp);

/* MANIFEST_H */
#endif
