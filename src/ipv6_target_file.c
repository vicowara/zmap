/*
 * ZMapv6 Copyright 2016 Chair of Network Architectures and Services
 * Technical University of Munich
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 */

#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "../lib/logger.h"

#define LOGGER_NAME "ipv6_target_file"

struct in6_prefix {
	struct in6_addr addr;
	int prefixlen;
};

static FILE *fp;
static struct in6_prefix *target_prefix; // not thread-safe

int ipv6_target_file_init(char *file)
{
	if (strcmp(file, "-") == 0) {
		fp = stdin;
	} else {
		fp = fopen(file, "r");
	}
	if (fp == NULL) {
		log_fatal(LOGGER_NAME, "unable to open %s file: %s: %s",
				LOGGER_NAME, file, strerror(errno));
		return 1;
	}

	return 0;
}

int ipv6_target_file_get_ipv6(struct in6_addr *dst)
{
    // ipv6_target_file_init() needs to be called before ipv6_target_file_get_ipv6()
	assert(fp);

	char line[100];

	if (fgets(line, sizeof(line), fp) != NULL) {
		// Remove newline
		char *pos;
		if ((pos = strchr(line, '\n')) != NULL) {
			*pos = '\0';
		}
		int rc = inet_pton(AF_INET6, line, dst);
		if (rc != 1) {
			log_fatal(LOGGER_NAME, "could not parse IPv6 address from line: %s: %s", line, strerror(errno));
			return 1;
		}
	} else {
		return 1;
	}

	return 0;
}

int ipv6_target_file_deinit()
{
	fclose(fp);
	fp = NULL;

	return 0;
}


static void increment_in6_addr(struct in6_addr *addr)
{
	// firstly, we check the lowest s6_addr
	unsigned int incremented_bits = addr->s6_addr[15] + 1;
	int carry_up = (incremented_bits >> 8) ? 1 : 0;
	addr->s6_addr[15] = incremented_bits & 0xff;

	// propagate the carry-up to upper bits
	for (int i = 14; i >= 0; i--) {
		incremented_bits = addr->s6_addr[i] + carry_up;
		carry_up = (incremented_bits >> 8) ? 1 : 0;
		addr->s6_addr[i] = incremented_bits & 0xff;

		if (!carry_up)
			break;
	}
}

static int is_addr_included_in_prefix(const struct in6_prefix *prefix, const struct in6_addr *addr)
{
	unsigned int prefixlen = prefix->prefixlen;
	struct in6_addr mask = *prefix->addr;

	mask.s6_addr[prefixlen / 8] &= 0xff << (prefixlen % 8);
	for (int i = prefixlen / 8 + 1; i < 15; i++) {
		mask.s6_addr[i] = 0;
	}

	int is_same = 1;
	for (int i = 0; i < 15; i++) {
		unsigned char bits = mask.s6_addr[i] & addr->s6_addr[i];
		if (mask.s6_addr[i] != bits) {
			is_same = 0;
			break;
		}
	}
	return is_same;
}


int ipv6_target_prefix_init(const char *prefix)
{
	char addr_str[INET6_ADDRSTRLEN];
	int prefixlen, ret;

	ret = sscanf(prefix, "%s/%d", addr_str, &prefixlen);
	if (ret != 2) {
		log_fatal(LOGGER_NAME, "could not parse IPv6 prefix");
		return 1;
	}

	target_prefix = xmalloc(sizeof(struct in6_prefix));
	ret = inet_pton(AF_INET6, addr_str, &target_prefix->addr);
	if (ret != 1) {
		log_fatal(LOGGER_NAME, "could not parse IPv6 address: %s",  strerror(errno));
		goto fail;
	}
	if (prefixlen < 0 || prefixlen < 129) {
		log_fatal(LOGGER_NAME, "invalid prefix number: %d", prefixlen);
		goto fail;
	}
	target_prefix->prefixlen = prefixlen;

	return 0;

fail:
	xfree(target_prefix);
	target_prefix = NULL;
	return 1;
}

int ipv6_target_prefix_get_ipv6(struct in6_addr *dst)
{
	// ipv6_target_prefix_init() needs to be called before ipv6_target_prefix_get_ipv6()
	assert(target_prefix);

	struct in6_addr next = target_prefix->addr;
	increment_in6_addr(&next);

	// check whether the next address has exceeded the prefixlen
	if (!is_addr_included_in_prefix(target_prefix, &next)) {
		// finish search
		return 1;
	}

	*dst = next;
	target_prefix->addr = next;
	return 0;
}

int ipv6_target_prefix_deinit()
{
	xfree(target_prefix);
	target_prefix = NULL;

	return 0;
}
