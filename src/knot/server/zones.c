#include <sys/stat.h>

#include "knot/server/zones.h"
#include "knot/other/error.h"
#include "knot/conf/conf.h"
#include "dnslib/zonedb.h"
#include "common/lists.h"
#include "dnslib/dname.h"
#include "dnslib/zone.h"
#include "dnslib/wire.h"
#include "knot/other/log.h"
#include "dnslib/zone-load.h"
#include "knot/other/debug.h"
#include "knot/server/xfr-in.h"
#include "knot/server/server.h"

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return SOA timer value.
 *
 * \param zone Pointer to zone.
 * \param rr_func RDATA specificator.
 * \return Timer in miliseconds.
 */
static uint32_t zones_soa_timer(dnslib_zone_t *zone,
				  uint32_t (*rr_func)(const dnslib_rdata_t*))
{
	uint32_t ret = 0;

	/* Retrieve SOA RDATA. */
	const dnslib_rrset_t *soa_rrs = 0;
	const dnslib_rdata_t *soa_rr = 0;
	soa_rrs = dnslib_node_rrset(dnslib_zone_apex(zone),
				    DNSLIB_RRTYPE_SOA);
	soa_rr = dnslib_rrset_rdata(soa_rrs);
	ret = rr_func(soa_rr);

	/* Convert to miliseconds. */
	return ret * 1000;
}

/*!
 * \brief Return SOA REFRESH timer value.
 *
 * \param zone Pointer to zone.
 * \return REFRESH timer in miliseconds.
 */
static uint32_t zones_soa_refresh(dnslib_zone_t *zone)
{
	return zones_soa_timer(zone, dnslib_rdata_soa_refresh);
}

/*!
 * \brief Return SOA RETRY timer value.
 *
 * \param zone Pointer to zone.
 * \return RETRY timer in miliseconds.
 */
static uint32_t zones_soa_retry(dnslib_zone_t *zone)
{
	return zones_soa_timer(zone, dnslib_rdata_soa_retry);
}

/*!
 * \brief Return SOA EXPIRE timer value.
 *
 * \param zone Pointer to zone.
 * \return EXPIRE timer in miliseconds.
 */
static uint32_t zones_soa_expire(dnslib_zone_t *zone)
{
	return zones_soa_timer(zone, dnslib_rdata_soa_expire);
}

/*!
 * \brief AXFR-IN expire event handler.
 */
static int zones_axfrin_expire(event_t *e)
{
	debug_zones("axfrin: EXPIRE timer event\n");
	dnslib_zone_t *zone = (dnslib_zone_t *)e->data;

	/* Cancel pending timers. */
	if (zone->xfr_in.timer) {
		evsched_cancel(e->caller, zone->xfr_in.timer);
		evsched_event_free(e->caller, zone->xfr_in.timer);
		zone->xfr_in.timer = 0;
	}

	/* Delete self. */
	evsched_event_free(e->caller, e);
	zone->xfr_in.expire = 0;
	zone->xfr_in.next_id = -1;

	/*! \todo Remove zone from database. */
	return 0;
}

/*!
 * \brief AXFR-IN poll event handler.
 */
static int zones_axfrin_poll(event_t *e)
{
	debug_zones("axfrin: REFRESH or RETRY timer event\n");
	dnslib_zone_t *zone = (dnslib_zone_t *)e->data;

	/* Get zone dname. */
	const dnslib_node_t *apex = dnslib_zone_apex(zone);
	const dnslib_dname_t *dname = dnslib_node_owner(apex);

	/* Prepare buffer for query. */
	uint8_t qbuf[SOCKET_MTU_SZ];
	size_t buflen = SOCKET_MTU_SZ;

	/* Create query. */
	int ret = xfrin_create_soa_query(dname, qbuf, &buflen);
	if (ret == KNOT_EOK && zone->xfr_in.ifaces) {

		int sock = -1;
		iface_t *i = 0;
		sockaddr_t *master = &zone->xfr_in.master;

		/*! \todo Bind to random port? xfr_master should then use some
		 *        polling mechanisms to handle incoming events along
		 *        with polled packets - evqueue should implement this.
		 */

		/* Lock RCU. */
		rcu_read_lock();

		/* Find suitable interface. */
		WALK_LIST(i, **zone->xfr_in.ifaces) {
			if (i->type[UDP_ID] == master->family) {
				sock = i->fd[UDP_ID];
				break;
			}
		}

		/* Unlock RCU. */
		rcu_read_unlock();

		/* Send query. */
		ret = -1;
		if (sock > -1) {
			ret = sendto(sock, qbuf, buflen, 0,
				     master->ptr, master->len);
		}

		/* Store ID of the awaited response. */
		if (ret == buflen) {
			zone->xfr_in.next_id = dnslib_wire_get_id(qbuf);
			debug_zones("axfrin: expecting SOA response ID=%d\n",
				    zone->xfr_in.next_id);
		}

	}

	/* Schedule EXPIRE timer on first attempt. */
	if (!zone->xfr_in.expire) {
		uint32_t expire_tmr = zones_soa_expire(zone);
		zone->xfr_in.expire = evsched_schedule_cb(
					      e->caller,
					      zones_axfrin_expire,
					      zone, expire_tmr);
		debug_zones("axfrin: scheduling EXPIRE timer after %u secs\n",
			    expire_tmr / 1000);
	}

	/* Reschedule as RETRY timer. */
	evsched_schedule(e->caller, e, zones_soa_retry(zone));
	debug_zones("axfrin: RETRY after %u secs\n",
		    zones_soa_retry(zone) / 1000);
	return ret;
}

/*!
 * \brief Update timers related to zone.
 *
 */
void zones_timers_update(dnslib_zone_t *zone, evsched_t *sch)
{
	/* Check AXFR-IN master server. */
	if (zone->xfr_in.master.ptr == 0) {
		return;
	}

	/* Schedule REFRESH timer. */
	uint32_t refresh_tmr = zones_soa_refresh(zone);
	zone->xfr_in.timer = evsched_schedule_cb(sch, zones_axfrin_poll,
						       zone, refresh_tmr);

	/* Cancel EXPIRE timer. */
	if (zone->xfr_in.expire) {
		evsched_cancel(sch, zone->xfr_in.expire);
		evsched_event_free(sch, zone->xfr_in.expire);
		zone->xfr_in.expire = 0;
	}
}

/*!
 * \brief Update ACL list from configuration.
 *
 * \param acl Pointer to existing or NULL ACL.
 * \param acl_list List of remotes from configuration.
 *
 * \retval KNOT_EOK on success.
 * \retval KNOT_EINVAL on invalid parameters.
 * \retval KNOT_ENOMEM on failed memory allocation.
 */
static int zones_set_acl(acl_t **acl, list* acl_list)
{
	if (!acl || !acl_list) {
		return KNOT_EINVAL;
	}

	/* Truncate old ACL. */
	acl_delete(acl);

	/* Create new ACL. */
	*acl = acl_new(ACL_DENY, 0);
	if (!*acl) {
		return KNOT_ENOMEM;
	}

	/* Load ACL rules. */
	conf_remote_t *r = 0;
	WALK_LIST(r, *acl_list) {

		/* Initialize address. */
		sockaddr_t addr;
		conf_iface_t *cfg_if = r->remote;
		int ret = sockaddr_set(&addr, cfg_if->family,
				       cfg_if->address, cfg_if->port);

		/* Load rule. */
		if (ret > 0) {
			acl_create(*acl, &addr, ACL_ACCEPT);
		}
	}

	return KNOT_EOK;
}

/*!
 * \brief Load zone to zone database.
 *
 * \param zonedb Zone database to load the zone into.
 * \param zone_name Zone name (owner of the apex node).
 * \param source Path to zone file source.
 * \param filename Path to requested compiled zone file.
 *
 * \retval KNOT_EOK
 * \retval KNOT_EINVAL
 * \retval KNOT_EZONEINVAL
 */
static int zones_load_zone(dnslib_zonedb_t *zonedb, const char *zone_name,
			   const char *source, const char *filename)
{
	dnslib_zone_t *zone = NULL;

	// Check path
	if (filename) {
		debug_server("Parsing zone database '%s'\n", filename);
		zloader_t *zl = dnslib_zload_open(filename);
		if (!zl) {
			log_server_error("Compiled db '%s' is too old, "
			                 " please recompile.\n",
			                 filename);
			return KNOT_EZONEINVAL;
		}

		// Check if the db is up-to-date
		int src_changed = strcmp(source, zl->source) != 0;
		if (src_changed || dnslib_zload_needs_update(zl)) {
			log_server_warning("Database for zone '%s' is not "
			                   "up-to-date. Please recompile.\n",
			                   zone_name);
		}

		zone = dnslib_zload_load(zl);
		if (zone) {
			// save the timestamp from the zone db file
			struct stat s;
			stat(filename, &s);
			dnslib_zone_set_version(zone, s.st_mtime);

			if (dnslib_zonedb_add_zone(zonedb, zone) != 0){
				dnslib_zone_deep_free(&zone, 0);
				zone = 0;
			}
		}

		dnslib_zload_close(zl);

		if (!zone) {
			log_server_error("Failed to load "
					 "db '%s' for zone '%s'.\n",
					 filename, zone_name);
			return KNOT_EZONEINVAL;
		}
	} else {
		/* db is null. */
		return KNOT_EINVAL;
	}

//	dnslib_zone_dump(zone, 1);

	return KNOT_EOK;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Fill the new database with zones.
 *
 * Zones that should be retained are just added from the old database to the
 * new. New zones are loaded.
 *
 * \param ns Name server instance.
 * \param zone_conf Zone configuration.
 * \param db_old Old zone database.
 * \param db_new New zone database.
 *
 * \return Number of inserted zones.
 */
static int zones_insert_zones(ns_nameserver_t *ns,
			      const list *zone_conf,
                              const dnslib_zonedb_t *db_old,
                              dnslib_zonedb_t *db_new)
{
	node *n = 0;
	int inserted = 0;
	// for all zones in the configuration
	WALK_LIST(n, *zone_conf) {
		conf_zone_t *z = (conf_zone_t *)n;
		// convert the zone name into a domain name
		dnslib_dname_t *zone_name = dnslib_dname_new_from_str(z->name,
		                                         strlen(z->name), NULL);
		if (zone_name == NULL) {
			log_server_error("Error creating domain name from zone"
			                 " name\n");
			return inserted;
		}

		debug_zones("Inserting zone %s into the new database.\n",
		            z->name);

		// try to find the zone in the current zone db
		dnslib_zone_t *zone = dnslib_zonedb_find_zone(db_old,
		                                              zone_name);
		int reload = 0;

		if (zone != NULL) {
			// if found, check timestamp of the file against the
			// loaded zone
			struct stat s;
			stat(z->file, &s);
			if (dnslib_zone_version(zone) < s.st_mtime) {
				// the file is newer, reload!
				reload = 1;
			}
		} else {
			reload = 1;
		}

		if (reload) {
			debug_zones("Not found in old database or the loaded"
			            " version is old, loading...\n");
			int ret = zones_load_zone(db_new, z->name,
						  z->file, z->db);
			if (ret != KNOT_EOK) {
				log_server_error("Error loading new zone to"
				                 " the new database: %s\n",
				                 knot_strerror(ret));
			} else {
				// Find the new zone
				zone = dnslib_zonedb_find_zone(db_new,
							       zone_name);
				++inserted;
			}
			// unused return value, if not loaded, just continue
		} else {
			// just insert the zone into the new zone db
			debug_zones("Found in old database, copying to new.\n");
			int ret = dnslib_zonedb_add_zone(db_new, zone);
			if (ret != KNOT_EOK) {
				log_server_error("Error adding old zone to"
				                 " the new database: %s\n",
				                 knot_strerror(ret));
			} else {
				++inserted;
			}
		}

		// Update ACLs
		if (zone) {
			debug_zones("Updating zone ACLs.");
			zones_set_acl(&zone->acl.xfr_out, &z->acl.xfr_out);
			zones_set_acl(&zone->acl.notify_in, &z->acl.notify_in);
			zones_set_acl(&zone->acl.notify_out, &z->acl.notify_out);

			// Update available interfaces
			zone->xfr_in.ifaces = &ns->server->ifaces;

			// Update master server address
			sockaddr_init(&zone->xfr_in.master, -1);
			if (!EMPTY_LIST(z->acl.xfr_in)) {
				conf_remote_t *r = HEAD(z->acl.xfr_in);
				conf_iface_t *cfg_if = r->remote;
				sockaddr_set(&zone->xfr_in.master,
					     cfg_if->family,
					     cfg_if->address,
					     cfg_if->port);
			}

			// Update events scheduled for zone
			zones_timers_update(zone, ns->server->sched);
		}

		dnslib_dname_free(&zone_name);
	}
	return inserted;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Remove zones present in the configuration from the old database.
 *
 * After calling this function, the old zone database should contain only zones
 * that should be completely deleted.
 *
 * \param zone_conf Zone configuration.
 * \param db_old Old zone database to remove zones from.
 *
 * \retval KNOT_EOK
 * \retval KNOT_ERROR
 */
static int zones_remove_zones(const list *zone_conf, dnslib_zonedb_t *db_old)
{
	node *n;
	// for all zones in the configuration
	WALK_LIST(n, *zone_conf) {
		conf_zone_t *z = (conf_zone_t *)n;
		// convert the zone name into a domain name
		dnslib_dname_t *zone_name = dnslib_dname_new_from_str(z->name,
		                                         strlen(z->name), NULL);
		if (zone_name == NULL) {
			log_server_error("Error creating domain name from zone"
			                 " name\n");
			return KNOT_ERROR;
		}
		debug_zones("Removing zone %s from the old database.\n",
		            z->name);
		// remove the zone from the old zone db, but do not delete it
		dnslib_zonedb_remove_zone(db_old, zone_name, 0);

		dnslib_dname_free(&zone_name);
	}
	return KNOT_EOK;
}

/*----------------------------------------------------------------------------*/
/* API functions                                                              */
/*----------------------------------------------------------------------------*/

int zones_update_db_from_config(const conf_t *conf, ns_nameserver_t *ns,
                               dnslib_zonedb_t **db_old)
{
	// Check parameters
	if (conf == NULL || ns == NULL) {
		return KNOT_EINVAL;
	}

	// Lock RCU to ensure noone will deallocate any data under our hands.
	rcu_read_lock();

	// Grab a pointer to the old database
	*db_old = ns->zone_db;
	if (*db_old == NULL) {
		log_server_error("Missing zone database in nameserver structure"
		                 ".\n");
		return KNOT_ERROR;
	}

	// Create new zone DB
	dnslib_zonedb_t *db_new = dnslib_zonedb_new();
	if (db_new == NULL) {
		return KNOT_ERROR;
	}

	log_server_info("Loading %d zones...\n", conf->zones_count);

	// Insert all required zones to the new zone DB.
	int inserted = zones_insert_zones(ns, &conf->zones, *db_old, db_new);

	log_server_info("Loaded %d out of %d zones.\n", inserted,
	                conf->zones_count);

	if (inserted != conf->zones_count) {
		log_server_warning("Not all the zones were loaded.\n");
	}

	debug_zones("Old db in nameserver: %p, old db stored: %p, new db: %p\n",
	            ns->zone_db, *db_old, db_new);

	// Switch the databases.
	(void)rcu_xchg_pointer(&ns->zone_db, db_new);

	debug_zones("db in nameserver: %p, old db stored: %p, new db: %p\n",
	            ns->zone_db, *db_old, db_new);

	/*
	 *  Remove all zones present in the new DB from the old DB.
	 *  No new thread can access these zones in the old DB, as the
	 *  databases are already switched.
	 */
	int ret = zones_remove_zones(&conf->zones, *db_old);
	if (ret != KNOT_EOK) {
		return ret;
	}

	// Unlock RCU, messing with any data will not affect us now
	rcu_read_unlock();

	debug_zones("Old database is empty (%p): %s\n", (*db_old)->zones,
	            skip_is_empty((*db_old)->zones) ? "yes" : "no");

	return KNOT_EOK;
}
