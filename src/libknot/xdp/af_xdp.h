/*  Copyright (C) 2019 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

#include <sys/socket.h>

typedef struct {
	struct sockaddr_storage ip_from;
	struct sockaddr_storage ip_to;
	uint8_t eth_from[6];
	uint8_t eth_to[6];
	struct iovec payload;
} knot_xsk_msg_t;

int knot_xsk_init(const char *ifname, const char *prog_fname,
                  ssize_t *out_busy_frames);

void knot_xsk_deinit(void);

struct iovec knot_xsk_alloc_frame(void);

int knot_xsk_sendmsg(const knot_xsk_msg_t *msg); // msg->payload MUST have been allocated by knot_xsk_alloc_frame()

int knot_xsk_sendmmsg(const knot_xsk_msg_t msgs[], uint32_t count); // skip messages with payload length == 0

int knot_xsk_recvmmsg(knot_xsk_msg_t msgs[], uint32_t max_count, uint32_t *count);

void knot_xsk_free_recvd(const knot_xsk_msg_t *msg);

int knot_xsk_check(void);

int knot_xsk_get_poll_fd(void);