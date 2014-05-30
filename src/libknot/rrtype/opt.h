/*!
 * \file opt.h
 *
 * \author Lubos Slovak <lubos.slovak@nic.cz>
 *
 * \brief Functions for manipulating the EDNS OPT pseudo-RR.
 *
 * \addtogroup libknot
 * @{
 */
/*  Copyright (C) 2014 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

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

#pragma once

#include <stdint.h>

#include "libknot/util/utils.h"
#include "libknot/rrset.h"

/* Forward declaration. */
struct knot_packet;

/*! \brief Various constants related to EDNS. */
enum knot_edns_const {
	/*! \brief Minimal UDP payload with EDNS enabled. */
	KNOT_EDNS_MIN_UDP_PAYLOAD = 512,
	/*! \brief Minimal payload when using DNSSEC (RFC4035/sec.3) */
	KNOT_EDNS_MIN_DNSSEC_PAYLOAD = 1220,
	/*! \brief Maximal UDP payload with EDNS enabled. */
	KNOT_EDNS_MAX_UDP_PAYLOAD = 4096,
	/*! \brief Supported EDNS version. */
	KNOT_EDNS_VERSION = 0,
	/*! \brief NSID option code. */
	KNOT_EDNS_OPTION_NSID     = 3,
	/*! \brief Minimum size of EDNS OPT RR in wire format. */
	KNOT_EDNS_MIN_SIZE        = 11,
	/*! \brief EDNS OPT header size. */
	KNOT_EDNS_OPTION_HDRLEN   = 2 * sizeof(uint16_t)
};

/*!
 * \brief EDNS DO flag.
 *
 * \note Use only with unsigned 2-byte variables.
 * \warning Flags are represented in machine byte order.
 */
static const uint16_t KNOT_EDNS_FLAG_DO = (uint16_t)1 << 15;

/*! \brief Extended RCODE BADVERS. */
static const uint8_t KNOT_EDNS_RCODE_BADVERS = 16;


/*----------------------------------------------------------------------------*/
/* EDNS OPT RR handling functions.                                            */
/*----------------------------------------------------------------------------*/

/*!
 * \brief Initialize OPT RR.
 *
 * \param max_pld   Max UDP payload.
 * \param ext_rcode Extended RCODE.
 * \param ver       Version.
 * \param mm        Memory context.
 *
 * \return KNOT_EOK or an error
 */
int knot_edns_init(knot_rrset_t *opt_rr, uint16_t max_pld,
                  uint8_t ext_rcode, uint8_t ver, mm_ctx_t *mm);


/*!
 * \brief Returns size of the OPT RR in wire format.
 *
 * \param opt_rr OPT RR to count the wire size of.
 *
 * \return Size of the OPT RR in bytes.
 */
size_t knot_edns_wire_size(knot_rrset_t *opt_rr);

/*!
 * \brief Returns the Max UDP payload value stored in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the value from.
 *
 * \return Max UDP payload in bytes.
 */
uint16_t knot_edns_get_payload(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the Max UDP payload field in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the value to.
 * \param payload UDP payload in bytes.
 */
void knot_edns_set_payload(knot_rrset_t *opt_rr, uint16_t payload);

/*!
 * \brief Returns the Extended RCODE stored in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the Extended RCODE from.
 *
 * \return Extended RCODE.
 */
uint8_t knot_edns_get_ext_rcode(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the Extended RCODE field in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the Extended RCODE to.
 * \param ext_rcode Extended RCODE to set.
 */
void knot_edns_set_ext_rcode(knot_rrset_t *opt_rr, uint8_t ext_rcode);

/*!
 * \brief Returns the EDNS version stored in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the EDNS version from.
 *
 * \return EDNS version.
 */
uint8_t knot_edns_get_version(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the EDNS version field in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the EDNS version to.
 * \param version EDNS version to set.
 */
void knot_edns_set_version(knot_rrset_t *opt_rr, uint8_t version);

/*!
 * \brief Returns the state of the DO bit in the OPT RR flags.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the DO bit from.
 *
 * \return <> 0 if the DO bit is set.
 * \return 0 if the DO bit is not set.
 */
bool knot_edns_do(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the DO bit in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the DO bit in.
 */
void knot_edns_set_do(knot_rrset_t *opt_rr);

/*!
 * \brief Adds EDNS Option to the OPT RR.
 *
 * \note The function now supports adding empty OPTION (just having its code).
 *       This does not make much sense now with NSID, but may be ok use later.
 *
 * \param opt_rr  OPT RR structure to add the Option to.
 * \param code    Option code.
 * \param length  Option data length in bytes.
 * \param data    Option data.
 *
 * \retval KNOT_EOK
 * \retval KNOT_ENOMEM
 */
int knot_edns_add_option(knot_rrset_t *opt_rr, uint16_t code,
                         uint16_t length, const uint8_t *data, mm_ctx_t *mm);

/*!
 * \brief Checks if the OPT RR contains Option with the specified code.
 *
 * \param opt_rr OPT RR structure to check for the Option in.
 * \param code Option code to check for.
 *
 * \retval <> 0 if the OPT RR contains Option with Option code \a code.
 * \retval 0 otherwise.
 */
bool knot_edns_has_option(const knot_rrset_t *opt_rr, uint16_t code);

/*! \brief Return true if RRSet has NSID option. */
bool knot_edns_has_nsid(const knot_rrset_t *opt_rr);

/*!
 * \brief Checks OPT RR semantics.
 *
 * Checks whether RDATA are OK, i.e. that all OPTIONs have proper lengths.
 *
 * \param opt_rr OPT RR to check.
 *
 * \return true if passed, false if failed
 */
bool knot_edns_check_record(knot_rrset_t *opt_rr);

/*! @} */
