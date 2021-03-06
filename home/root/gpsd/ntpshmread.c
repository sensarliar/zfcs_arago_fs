/* ntpshmread.c -- monitor the inner end of an ntpshmwrite.connection
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 * Some of this was swiped from the NTPD distribution.
 */
#define _XOPEN_SOURCE 600
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S*/

#include "ntpshm.h"
#include "compiler.h"

struct shmTime /*@null@*/ *shm_get(const int unit, const bool create, const bool forall)
/* initialize a SHM segment */
{
    /*@-mustfreefresh@*/
    struct shmTime *p = NULL;
    int shmid;

    /*
     * Big units will give non-ASCII but that's OK
     * as long as everybody does it the same way.
     */
    shmid = shmget((key_t)(NTPD_BASE + unit), sizeof(struct shmTime),
		   (create ? IPC_CREAT : 0) | (forall ? 0666 : 0600));
    if (shmid == -1) { /* error */
	return NULL;
    }
    p = (struct shmTime *)shmat (shmid, 0, 0);
    if (p == (struct shmTime *)-1) { /* error */
	return NULL;
    }
    return p;
    /*@+mustfreefresh@*/
}

/*@-statictrans@*/
char *ntp_name(const int unit)
/* return the name of a specified segment */
{
    static char name[5] = "NTP\0";

    /*@i2@*/name[3] = '0' + (char)unit;

    return name;
}
/*@+statictrans@*/

enum segstat_t ntp_read(/*@null@*/struct shmTime *shm_in, /*@out@*/struct shm_stat_t *shm_stat, const bool consume)
/* try to grab a sample from the specified SHM segment */
{
    volatile struct shmTime shmcopy, *shm = shm_in;
    volatile int cnt;

    unsigned int cns_new, rns_new;

    if (shm == NULL) {
	shm_stat->status = NO_SEGMENT;
	return NO_SEGMENT;
    }

    /*@-type@*//* splint is confused about struct timespec */
    shm_stat->tvc.tv_sec = shm_stat->tvc.tv_nsec = 0;

    clock_gettime(CLOCK_REALTIME, &shm_stat->tvc);

    /* relying on word access to be atomic here */
    if (shm->valid == 0) {
	shm_stat->status = NOT_READY;
	return NOT_READY;
    }

    cnt = shm->count;

    /*
     * This is proof against concurrency issues if either (a) the
     * memory_barrier() call works on this host, or (b) memset
     * compiles to an uninterruptible single-instruction bitblt (this
     * will probably cease to be true if the structure exceeds your VM
     * page size).
     */
    memory_barrier();
    memcpy((void *)&shmcopy, (void *)shm, sizeof(struct shmTime));

    /*
     * An update consumer such as ntpd should zero the valid flag at this point.
     * A program snooping the updates to collect statistics should not, lest
     * it make the data unavailable for consumers.
     */
    if (consume)
	shm->valid = 0;
    memory_barrier();

    /* 
     * Clash detection in case neither (a) nor (b) was true.
     * Not supported in mode 0, and word access to the count field 
     * must be atomic for this to work.
     */
    if (shmcopy.mode > 0 && cnt != shm->count) {
	shm_stat->status = CLASH;
	return shm_stat->status;
    }

    shm_stat->status = OK;

    switch (shmcopy.mode) {
    case 0:
	shm_stat->tvr.tv_sec	= shmcopy.receiveTimeStampSec;
	shm_stat->tvr.tv_nsec	= shmcopy.receiveTimeStampUSec * 1000;
	rns_new		= shmcopy.receiveTimeStampNSec;
	shm_stat->tvt.tv_sec	= shmcopy.clockTimeStampSec;
	shm_stat->tvt.tv_nsec	= shmcopy.clockTimeStampUSec * 1000;
	cns_new		= shmcopy.clockTimeStampNSec;

	/* Since the following comparisons are between unsigned
	** variables they are always well defined, and any
	** (signed) underflow will turn into very large unsigned
	** values, well above the 1000 cutoff.
	**
	** Note: The usecs *must* be a *truncated*
	** representation of the nsecs. This code will fail for
	** *rounded* usecs, and the logic to deal with
	** wrap-arounds in the presence of rounded values is
	** much more convoluted.
	*/
	if (((cns_new - (unsigned)shm_stat->tvt.tv_nsec) < 1000)
	       && ((rns_new - (unsigned)shm_stat->tvr.tv_nsec) < 1000)) {
	    shm_stat->tvt.tv_nsec = cns_new;
	    shm_stat->tvr.tv_nsec = rns_new;
	}
	/* At this point shm_stat->tvr and shm_stat->tvt contain valid ns-level
	** timestamps, possibly generated by extending the old
	** us-level timestamps
	*/
	break;

    case 1:
	shm_stat->tvr.tv_sec	= shmcopy.receiveTimeStampSec;
	shm_stat->tvr.tv_nsec	= shmcopy.receiveTimeStampUSec * 1000;
	rns_new		= shmcopy.receiveTimeStampNSec;
	shm_stat->tvt.tv_sec	= shmcopy.clockTimeStampSec;
	shm_stat->tvt.tv_nsec	= shmcopy.clockTimeStampUSec * 1000;
	cns_new		= shmcopy.clockTimeStampNSec;
		
	/* See the case above for an explanation of the
	** following test.
	*/
	if (((cns_new - (unsigned)shm_stat->tvt.tv_nsec) < 1000)
	       && ((rns_new - (unsigned)shm_stat->tvr.tv_nsec) < 1000)) {
	    shm_stat->tvt.tv_nsec = cns_new;
	    shm_stat->tvr.tv_nsec = rns_new;
	}
	/* At this point shm_stat->tvr and shm_stat->tvt contains valid ns-level
	** timestamps, possibly generated by extending the old
	** us-level timestamps
	*/
	break;

    default:
	shm_stat->status = BAD_MODE;
	break;
    }
    /*@-type@*/

    /*
     * leap field is not a leap offset but a leap notification code.
     * The values are magic numbers used by NTP and set by GPSD, if at all, in
     * the subframe code.
     */
    shm_stat->leap = shmcopy.leap;
    shm_stat->precision = shmcopy.precision;

    return shm_stat->status;
}

/* end */
