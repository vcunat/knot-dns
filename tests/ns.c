/*  Copyright (C) 2013 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <tap/basic.h>

#include "common/mempool.h"
#include "common/descriptor.h"
#include "libknot/packet/wire.h"
#include "libknot/nameserver/name-server.h"
#include "libknot/nameserver/ns_proc_query.h"
#include "knot/server/zones.h"

/* root zone query */
#define IN_QUERY_LEN 28
#define IN_QUERY_QTYPE_POS (KNOT_WIRE_HEADER_SIZE + 1)
static const uint8_t IN_QUERY[IN_QUERY_LEN] = {
	0xac, 0x77, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, /* header */
        0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x29,
	0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* CH TXT id.server */
#define CH_QUERY_LEN 27
static const uint8_t CH_QUERY[CH_QUERY_LEN] = {
	0xa0, 0xa2, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, /* header */
        0x02, 0x69, 0x64, 0x06, 0x73, 0x65, 0x72, 0x76,
	0x65, 0x72, 0x00, 0x00, 0x10, 0x00, 0x03
};

/* SOA RDATA. */
#define SOA_RDLEN 30
static const uint8_t SOA_RDATA[SOA_RDLEN] = {
        0x02, 0x6e, 0x73, 0x00,        /* ns. */
        0x04, 'm', 'a', 'i', 'l', 0x00,/* mail. */
        0x77, 0xdf, 0x1e, 0x63,        /* serial */
        0x00, 0x01, 0x51, 0x80,        /* refresh */
        0x00, 0x00, 0x1c, 0x20,        /* retry */
        0x00, 0x0a, 0x8c, 0x00,        /* expire */
        0x00, 0x00, 0x0e, 0x10         /* min ttl */
};

/* Basic response check (4 checks). */
static void answer_sanity_check(const uint8_t *query, const uint8_t *ans,
                                uint16_t ans_len, int exp_rcode, const char *name)
{
	ok(ans_len > KNOT_WIRE_HEADER_SIZE, "ns: len(%s answer) > DNS header", name);
	ok(knot_wire_get_qr(ans), "ns: %s answer has QR=1", name);
	is_int(exp_rcode, knot_wire_get_rcode(ans), "ns: %s answer RCODE=%d", name, exp_rcode);
	is_int(knot_wire_get_id(query), knot_wire_get_id(ans), "ns: %s MSGID match", name);
}

/* Resolve query and check answer for sanity. */
static void do_query(ns_proc_context_t *query_ctx, const char *name,
                     const uint8_t *query, uint16_t query_len,
                     uint8_t *ans, int exp_rcode)
{
	uint8_t wire[KNOT_WIRE_MAX_PKTSIZE];
	memcpy(wire, query, query_len);
	
	int state = ns_proc_in(wire, query_len, query_ctx);
	ok(state == NS_PROC_FULL || state == NS_PROC_FAIL, "ns: process %s query", name);
	uint16_t ans_len = KNOT_WIRE_MAX_PKTSIZE;
	state = ns_proc_out(ans, &ans_len, query_ctx);
	if (state == NS_PROC_FAIL) { /* Allow 1 generic error response. */
		state = ns_proc_out(ans, &ans_len, query_ctx);
	}
	ok(state == NS_PROC_FINISH, "ns: answer %s query", name);
	answer_sanity_check(query, ans, ans_len, exp_rcode, name);
}

int main(int argc, char *argv[])
{
	log_init();
	plan(6*6 + 1);

	/* Prepare. */
	int state = NS_PROC_FAIL;
	uint8_t wire[KNOT_WIRE_MAX_PKTSIZE], src[KNOT_WIRE_MAX_PKTSIZE];
	uint16_t wire_len = KNOT_WIRE_MAX_PKTSIZE, src_len = KNOT_WIRE_MAX_PKTSIZE;

	/* Create fake name server. */
	knot_nameserver_t *ns = knot_ns_create();
	ns->opt_rr = knot_edns_new();
	knot_edns_set_version(ns->opt_rr, EDNS_VERSION); 
	knot_edns_set_payload(ns->opt_rr, 4096);
	ns->identity = "bogus.ns";
	ns->version = "0.11";

	/* Insert root zone. */
	knot_dname_t *root_name = knot_dname_from_str(".");
	knot_node_t *apex = knot_node_new(root_name, NULL, 0);
	knot_rrset_t *soa_rrset = knot_rrset_new(root_name,
	                                         KNOT_RRTYPE_SOA, KNOT_CLASS_IN,
	                                         7200);
	knot_rrset_add_rdata(soa_rrset, SOA_RDATA, SOA_RDLEN);
	knot_node_add_rrset(apex, soa_rrset);
	knot_zone_t *root = knot_zone_new(apex);

	/* Stub data. */
	root->data = malloc(sizeof(zonedata_t));
	memset(root->data, 0, sizeof(zonedata_t));
	
	/* Bake the zone. */
	knot_node_t *first_nsec3 = NULL, *last_nsec3 = NULL;
	knot_zone_contents_adjust(root->contents, &first_nsec3, &last_nsec3, false);

	knot_zonedb_free(&ns->zone_db);
	ns->zone_db = knot_zonedb_new(1);
	knot_zonedb_insert(ns->zone_db, root);
	knot_zonedb_build_index(ns->zone_db);
	assert(knot_zonedb_find(ns->zone_db, root_name));

	/* Create processing context. */
	ns_proc_context_t query_ctx;
	memset(&query_ctx, 0, sizeof(ns_proc_context_t));
	mm_ctx_mempool(&query_ctx.mm, sizeof(knot_pkt_t));
	query_ctx.ns = ns;
	
	/* Create query processing parameter. */
	struct ns_proc_query_param param;
	sockaddr_set(&param.query_source, AF_INET, "127.0.0.1", 53);

	/* Query processor (valid input). */
	state = ns_proc_begin(&query_ctx, &param, NS_PROC_QUERY);
	do_query(&query_ctx, "IN/root", IN_QUERY, IN_QUERY_LEN, wire, KNOT_RCODE_NOERROR);

	/* Query processor (CH zone) */
	state = ns_proc_reset(&query_ctx);
	do_query(&query_ctx, "CH TXT", CH_QUERY, CH_QUERY_LEN, wire, KNOT_RCODE_NOERROR);
	
	/* Query processor (invalid input). */
	state = ns_proc_reset(&query_ctx);
	do_query(&query_ctx, "IN/formerr", IN_QUERY, IN_QUERY_LEN - 1, wire, KNOT_RCODE_FORMERR);

	/* Forge NOTIFY query from SOA query. */
	state = ns_proc_reset(&query_ctx);
	memcpy(src, IN_QUERY, IN_QUERY_LEN);
	src_len = IN_QUERY_LEN;
	wire_len = sizeof(wire);
	knot_wire_set_opcode(src, KNOT_OPCODE_NOTIFY);
	do_query(&query_ctx, "IN/notify", src, src_len, wire, KNOT_RCODE_NOTAUTH);
	
	/* Forge AXFR query. */
	ns_proc_reset(&query_ctx);
	memcpy(src, IN_QUERY, IN_QUERY_LEN);
	src_len = IN_QUERY_LEN;
	wire_len = sizeof(wire);
	knot_wire_write_u16(src + IN_QUERY_QTYPE_POS, KNOT_RRTYPE_AXFR);
	do_query(&query_ctx, "IN/axfr", src, src_len, wire, KNOT_RCODE_NOTAUTH);
	
	/* Forge IXFR query (badly formed, no SOA in NS). */
	ns_proc_reset(&query_ctx);
	memcpy(src, IN_QUERY, IN_QUERY_LEN);
	src_len = IN_QUERY_LEN;
	wire_len = sizeof(wire);
	knot_wire_write_u16(src + IN_QUERY_QTYPE_POS, KNOT_RRTYPE_IXFR);
	do_query(&query_ctx, "IN/ixfr-formerr", src, src_len, wire, KNOT_RCODE_FORMERR);

	/* #10 Process UPDATE query. */

	/* #10 Process AXFR client. */

	/* #10 Process IXFR client. */

	/* Finish. */
	state = ns_proc_finish(&query_ctx);
	ok(state == NS_PROC_NOOP, "ns: processing end" );

	/* Cleanup. */
	free(root->data);
	mp_delete((struct mempool *)query_ctx.mm.ctx);
	knot_ns_destroy(&ns);

	return 0;
}
