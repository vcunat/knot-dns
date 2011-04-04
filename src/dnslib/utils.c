#include <config.h>
#include <string.h>

#include "dnslib/dnslib-common.h"
#include "dnslib/utils.h"

/*----------------------------------------------------------------------------*/

dnslib_lookup_table_t *dnslib_lookup_by_name(dnslib_lookup_table_t *table,
                                             const char *name)
{
	while (table->name != NULL) {
		if (strcasecmp(name, table->name) == 0) {
			return table;
		}
		table++;
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/

dnslib_lookup_table_t *dnslib_lookup_by_id(dnslib_lookup_table_t *table,
                                           int id)
{
	while (table->name != NULL) {
		if (table->id == id) {
			return table;
		}
		table++;
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/

size_t dnslib_strlcpy(char *dst, const char *src, size_t size)
{
	char *d = dst;
	const char *s = src;
	size_t n = size;

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0) {
				break;
			}
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (size != 0) {
			*d = '\0';        /* NUL-terminate dst */
		}
		while (*s++)
			;
	}

	return(s - src - 1);        /* count does not include NUL */
}

unsigned dnslib_lcg_rand()
{
	static unsigned x = time(0);
	return x = 1664525 * x + 1013904223;
}
