/*******************************************************************************
** Copyright (c) 2005 Silicon Graphics, Inc. All Rights Reserved.
** Copyright (c) 2007,2008 William Hachfeld. All Rights Reserved.
** Copyright (c) 2007-2016 Krell Institute.  All Rights Reserved.
**
** This library is free software; you can redistribute it and/or modify it under
** the terms of the GNU Lesser General Public License as published by the Free
** Software Foundation; either version 2.1 of the License, or (at your option)
** any later version.
**
** This library is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
** details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this library; if not, write to the Free Software Foundation, Inc.,
** 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*******************************************************************************/

/** @file
 *
 * Declaration and definition of the HWCTime collector's runtime.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "KrellInstitute/Messages/DataHeader.h"
#include "KrellInstitute/Messages/Hwctime.h"
#include "KrellInstitute/Messages/Hwctime_data.h"
#include "KrellInstitute/Messages/ToolMessageTags.h"
#include "KrellInstitute/Messages/Thread.h"
#include "KrellInstitute/Messages/ThreadEvents.h"
#include "KrellInstitute/Services/Assert.h"
#include "KrellInstitute/Services/Collector.h"
#include "KrellInstitute/Services/Common.h"
#include "KrellInstitute/Services/Context.h"
#include "KrellInstitute/Services/Data.h"
#include "KrellInstitute/Services/PapiAPI.h"
#include "KrellInstitute/Services/Time.h"
#include "KrellInstitute/Services/Timer.h"
#include "KrellInstitute/Services/Unwind.h"
#include "KrellInstitute/Services/TLS.h"

#if UNW_TARGET_X86 || UNW_TARGET_X86_64
# define STACK_SIZE     (128*1024)      /* On x86, SIGSTKSZ is too small */
#else
# define STACK_SIZE     SIGSTKSZ
#endif

/*
 * NOTE: For some reason GCC doesn't like it when the following two macros are
 *       replaced with constant unsigned integers. It complains about the arrays
 *       in the tls structure being "variable-size type declared outside of any
 *       function" even though the size IS constant... Maybe this can be fixed?
 */

/** Number of entries in the sample buffer. */
#define CBTF_USERTIME_BUFFERSIZE  1024

/** Man number of frames for callstack collection */
#define CBTF_USERTIME_MAXFRAMES 100




/** String uniquely identifying this collector. */
const char* const cbtf_collector_unique_id = "hwctime";
#if defined(CBTF_SERVICE_USE_FILEIO)
const char* const data_suffix = "cbtf-data";
#endif

/** Type defining the items stored in thread-local storage. */
typedef struct {

    CBTF_DataHeader header;  /**< Header for following data blob. */
    CBTF_hwctime_data data;        /**< Actual data blob. */

    /** Sample buffer. */
    /**< buffer.stacktraces: Stack trace (PC) addresses. */
    /**< buffer.count: count value greater than 0 is top */
    /**< of stack. A count of 255 indicates */
    /**< another instance of this stack may */
    /**< exist in buffer stacktraces. */
    CBTF_StackTraceData buffer;

#if defined (HAVE_OMPT)
    /* these are ompt specific. */
    bool thread_idle, thread_wait_barrier;
    bool debug_collector_ompt;
#endif

    bool debug_collector;

    bool defer_sampling;
    int EventSet;

} TLS;

static int hwctime_papi_init_done = 0;

#ifdef USE_EXPLICIT_TLS

/**
 * Key used to look up our thread-local storage. This key <em>must</em> be
 * unique from any other key used by any of the CBTF services.
 */
static const uint32_t TLSKey = 0x00001EF6;

#else

/** Thread-local storage. */
static __thread TLS the_tls;

#endif


#if defined (HAVE_OMPT)
/* these are ompt specific functions to shift sample to an
 * OMPT defined blame.  These are only useful in a sampling
 * context such as pcsamp,hwcsamp,hwc,hwctime,usertime.
 */
void cbtf_thread_idle(bool flag) {
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    if (tls == NULL)
	return;
    tls->thread_idle=flag;
}

void cbtf_thread_barrier(bool flag) {
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    if (tls == NULL)
	return;
#if 0
    // this is not in use for now. we are not interested in barrier.
    // just the wait_barriers...
    tls->thread_barrier=flag;
#endif
}

void cbtf_thread_wait_barrier(bool flag) {
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    if (tls == NULL)
	return;
    tls->thread_wait_barrier=flag;
}

/** these names are aliases to the internal cbtf krell callacks.
 * We would like the users to see a more meaningful name in the views.
**/
void OMPT_THREAD_IDLE(bool) __attribute__ ((weak, alias ("cbtf_thread_idle")));
void OMPT_THREAD_WAIT_BARRIER(bool) __attribute__ ((weak, alias ("cbtf_thread_wait_barrier")));
#endif // if defined HAVE_OMPT

/**
 * Initialize the performance data header and blob contained within the given
 * thread-local storage. This function <em>must</em> be called before any of
 * the collection routines attempts to add a message.
 *
 * @param tls    Thread-local storage to be initialized.
 */
static void initialize_data(TLS* tls)
{
    Assert(tls != NULL);

    tls->header.time_begin = CBTF_GetTime();
    tls->header.time_end = 0;
    tls->header.addr_begin = ~0;
    tls->header.addr_end = 0;
    
    /* Re-initialize the actual data blob */
    tls->data.stacktraces.stacktraces_val = tls->buffer.stacktraces;
    tls->data.stacktraces.stacktraces_len = 0;
    tls->data.count.count_val = tls->buffer.count;
    tls->data.count.count_len = 0;

    /* Re-initialize the sampling buffer */
    memset(tls->buffer.stacktraces, 0, sizeof(tls->buffer.stacktraces));
    memset(tls->buffer.count, 0, sizeof(tls->buffer.count));
}


/**
 * Update the performance data header contained within the given thread-local
 * storage with the specified time. Insures that the time interval defined by
 * time_begin and time_end contain the specified time.
 *
 * @param tls     Thread-local storage to be updated.
 * @param time    Time with which to update.
 */
inline void update_header_with_time(TLS* tls, uint64_t time)
{
    Assert(tls != NULL);

    if (time < tls->header.time_begin)
    {
        tls->header.time_begin = time;
    }
    if (time >= tls->header.time_end)
    {
        tls->header.time_end = time + 1;
    }
}



/**
 * Update the performance data header contained within the given thread-local
 * storage with the specified address. Insures that the address range defined
 * by addr_begin and addr_end contain the specified address.
 *
 * @param tls     Thread-local storage to be updated.
 * @param addr    Address with which to update.
 */
inline void update_header_with_address(TLS* tls, uint64_t addr)
{
    Assert(tls != NULL);

    if (addr < tls->header.addr_begin)
    {
        tls->header.addr_begin = addr;
    }
    if (addr >= tls->header.addr_end)
    {
        tls->header.addr_end = addr + 1;
    }
}


/**
 * Send samples.
 *
 * This function can be called from within the sigprof handler and therefore
 * must be signal safe.  no strdup and friends.
 */
static void send_samples (TLS* tls)
{
    Assert(tls != NULL);

    tls->header.time_end = CBTF_GetTime();
    /* rank is not filled until mpi_init finished. safe to set here*/
    tls->header.rank = monitor_mpi_comm_rank();

#ifndef NDEBUG
	if (getenv("CBTF_DEBUG_COLLECTOR") != NULL) {
	    fprintf(stderr, "hwctime send_samples:\n");
	    fprintf(stderr, "time_range(%#lu,%#lu) addr range[%#lx, %#lx] stacktraces_len(%d) count_len(%d)\n",
		tls->header.time_begin,tls->header.time_end,
		tls->header.addr_begin,tls->header.addr_end,
		tls->data.stacktraces.stacktraces_len,
		tls->data.count.count_len);
	}
#endif

    cbtf_collector_send(&(tls->header), (xdrproc_t)xdr_CBTF_hwctime_data, &(tls->data));

    /* Re-initialize the data blob's header */
    initialize_data(tls);
}

static int total = 0;
static int stacktotal = 0;

/**
 * PAPI event handler.
 *
 * Called by PAPI_overflow each time a sample is to be taken. 
 * Extract the PC address for each frame in the current stack trace and store
 * them into the sample buffer. Terminate each stack trace with a NULL address.
 * When the sample buffer is full, it is sent to the framework
 * for storage in the experiment's database.
 *
 * @note    
 * 
 * @param context    Thread context at papi overflow.
 */
static void
hwctimePAPIHandler(int EventSet, void *address, long_long overflow_vector, void* context)
{
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    Assert(tls != NULL);

    if(tls->defer_sampling == true) {
        return;
    }
 

    int framecount = 0;
    int stackindex = 0;
    uint64_t framebuf[CBTF_USERTIME_MAXFRAMES];

    memset(framebuf,0, sizeof(framebuf));

    /* get stack address for current context and store them into framebuf. */

#if defined(__linux) && defined(__x86_64)
    /* The latest version of libunwind provides a fast trace
     * backtrace function we now use. We need to manually
     * skip signal frames when using that unwind function.
     * For PAPI's handler we need to skip 6 frames of
     * overhead.
     */

#if defined(USE_FASTTRACE)
    CBTF_GetStackTrace(FALSE, 6,
                        CBTF_USERTIME_MAXFRAMES /* maxframes*/, &framecount, framebuf) ;
#else
    CBTF_GetStackTraceFromContext (context, TRUE, 0,
                        CBTF_USERTIME_MAXFRAMES /* maxframes*/, &framecount, framebuf) ;
#endif

#else
    CBTF_GetStackTraceFromContext (context, TRUE, 0,
                        CBTF_USERTIME_MAXFRAMES /* maxframes*/, &framecount, framebuf) ;
#endif


#if defined (HAVE_OMPT)
    /* these are ompt specific.*/
    if (tls->thread_idle) {
	/* ompt. thread is in __kmp_wait_sleep from intel libomp runtime.
	 * sample count here is attributed as an idle.  Note that the sample
	 * PC address may be also be in any calls made by __kmp_wait_sleep
	 * while the ompt interface is in the idle state.
	 */
	framebuf[0] = CBTF_GetAddressOfFunction(OMPT_THREAD_IDLE);
    }

    if (tls->thread_wait_barrier) {
	/* ompt. thread is in __kmp_wait_sleep from intel libomp runtime.
	 * sample count here is attributed as a wait_barrier.  Note that the sample
	 * PC address may be also be in any calls made by __kmp_wait_sleep
	 * while the ompt interface is in the wait_barrier state.
	 */
	framebuf[0] = CBTF_GetAddressOfFunction(OMPT_THREAD_WAIT_BARRIER);
    }
#endif // if defined (HAVE_OMPT)

    bool_t stack_already_exists = FALSE;

    int i, j;
    /* search individual stacks via count/indexing array */
    for (i = 0; i < tls->data.count.count_len ; i++ )
    {
	/* a count > 0 indexes the top of stack in the data buffer. */
	/* a count == 255 indicates this stack is at the count limit. */

	if (tls->buffer.count[i] == 0) {
	    continue;
	}
	if (tls->buffer.count[i] == 255) {
	    continue;
	}

	/* see if the stack addresses match */
	for (j = 0; j < framecount ; j++ )
	{
	    if ( tls->buffer.stacktraces[i+j] != framebuf[j] ) {
		   break;
	    }
	}

	if ( j == framecount) {
	    stack_already_exists = TRUE;
	    stackindex = i;
	}
    }

    /* if the stack already exisits in the buffer, update its count
     * and return. If the stack is already at the count limit.
    */
    if (stack_already_exists && tls->buffer.count[stackindex] < 255 ) {
	/* update count for this stack */
	tls->buffer.count[stackindex] = tls->buffer.count[stackindex] + 1;
	return;
    }

    /* sample buffer has no room for these stack frames.*/
    int buflen = tls->data.stacktraces.stacktraces_len + framecount;
    if ( buflen > CBTF_USERTIME_BUFFERSIZE) {
	/* send the current sample buffer. (will init a new buffer) */
	send_samples(tls);
    }

    /* add frames to sample buffer, compute addresss range */
    for (i = 0; i < framecount ; i++)
    {
	/* always add address to buffer bt */
	tls->buffer.stacktraces[tls->data.stacktraces.stacktraces_len] = framebuf[i];

	/* top of stack indicated by a positive count. */
	/* all other elements are 0 */
	if (i > 0 ) {
	    tls->buffer.count[tls->data.count.count_len] = 0;
	} else {
	    tls->buffer.count[tls->data.count.count_len] = 1;
	}

	if (framebuf[i] < tls->header.addr_begin ) {
	    tls->header.addr_begin = framebuf[i];
	}
	if (framebuf[i] > tls->header.addr_end ) {
	    tls->header.addr_end = framebuf[i];
	}
	tls->data.stacktraces.stacktraces_len++;
	tls->data.count.count_len++;
    }
}


/**
 * Called by the CBTF collector service in order to start data collection.
 */
void cbtf_collector_start(const CBTF_DataHeader* const header)
{
/**
 * Start sampling.
 *
 * Starts hardware counter (HWC) sampling for the thread executing this
 * function. Initializes the appropriate thread-local data structures and
 * then enables the sampling counter.
 *
 * @param arguments    Encoded function arguments.
 */
    /* Create and access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = malloc(sizeof(TLS));
    Assert(tls != NULL);
    CBTF_SetTLS(TLSKey, tls);
#else
    TLS* tls = &the_tls;
#endif
    Assert(tls != NULL);

    tls->defer_sampling=false;

    if (getenv("CBTF_DEBUG_COLLECTOR") != NULL) {
	tls->debug_collector = true;
    } else {
	tls->debug_collector = false;
    }

#if defined (HAVE_OMPT)
    if (getenv("CBTF_DEBUG_COLLECTOR_OMPT") != NULL) {
	tls->debug_collector_ompt = true;
    } else {
	tls->debug_collector_ompt = false;
    }
#endif

    /* Decode the passed function arguments */
    CBTF_hwctime_start_sampling_args args;
    memset(&args, 0, sizeof(args));
 

    /* set defaults */ 
    int hwctime_papithreshold = THRESHOLD*2;
    char* hwctime_papi_event = "PAPI_TOT_CYC";

    char* hwctime_event_param = getenv("CBTF_HWCTIME_EVENT");
    if (hwctime_event_param != NULL) {
        hwctime_papi_event=hwctime_event_param;
    }

    const char* sampling_rate = getenv("CBTF_HWCTIME_THRESHOLD");
    if (sampling_rate != NULL) {
        hwctime_papithreshold=atoi(sampling_rate);
    }
    tls->data.interval = hwctime_papithreshold;

#if defined(CBTF_SERVICE_USE_FILEIO)
    CBTF_SetSendToFile(cbtf_collector_unique_id, "cbtf-data");
#endif

    /* Initialize the actual data blob */
    memcpy(&tls->header, header, sizeof(CBTF_DataHeader));
    initialize_data(tls);


    /* We can not assign mpi rank in the header at this point as it may not
     * be set yet. assign an integer tid value.  omp_tid is used regardless of
     * whether the application is using openmp threads.
     * libmonitor uses the same numbering scheme as openmp.
     */
    tls->header.omp_tid = monitor_get_thread_num();
    tls->header.id = strdup(cbtf_collector_unique_id);
    tls->header.time_begin = CBTF_GetTime();

    if(hwctime_papi_init_done == 0) {
	CBTF_init_papi();
	tls->EventSet = PAPI_NULL;
	hwctime_papi_init_done = 1;
    }

    unsigned papi_event_code = get_papi_eventcode(hwctime_papi_event);

    /* PAPI SETUP */
    CBTF_Create_Eventset(&tls->EventSet);
    CBTF_AddEvent(tls->EventSet, papi_event_code);
    CBTF_Overflow(tls->EventSet, papi_event_code,
		    hwctime_papithreshold, hwctimePAPIHandler);

    /* Begin sampling */
    tls->header.time_begin = CBTF_GetTime();
    CBTF_Start(tls->EventSet);
}



/**
 * Called by the CBTF collector service in order to pause data collection.
 */
void cbtf_collector_pause()
{
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    if (hwctime_papi_init_done == 0 || tls == NULL)
	return;

    tls->defer_sampling=true;
}



/**
 * Called by the CBTF collector service in order to resume data collection.
 */
void cbtf_collector_resume()
{
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    if (hwctime_papi_init_done == 0 || tls == NULL)
	return;

    tls->defer_sampling=false;
}


#ifdef USE_EXPLICIT_TLS
void destroy_explicit_tls() {
    TLS* tls = CBTF_GetTLS(TLSKey);
    /* Destroy our thread-local storage */
    if (tls) {
        free(tls);
    }
    CBTF_SetTLS(TLSKey, NULL);
}
#endif


/**
 * Called by the CBTF collector service in order to stop data collection.
 */
void cbtf_collector_stop()
{
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    Assert(tls != NULL);

    if (tls->EventSet == PAPI_NULL) {
	/*fprintf(stderr,"hwctime_stop_sampling RETURNS - NO EVENTSET!\n");*/
	/* we are called before eny events are set in papi. just return */
        return;
    }

    /* Stop sampling */
    CBTF_Stop(tls->EventSet, NULL);

    tls->header.time_end = CBTF_GetTime();

    /* Are there any unsent samples? */
    if(tls->data.stacktraces.stacktraces_len > 0) {
	/* Send these samples */
	send_samples(tls);
    }

    /* Destroy our thread-local storage */
#ifdef CBTF_SERVICE_USE_EXPLICIT_TLS
    destroy_explicit_tls();
#endif
}



// UNUSED at this time.
#if defined (CBTF_SERVICE_USE_OFFLINE)
void hwctime_collector_events_start()
{
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    if (hwctime_papi_init_done == 0 || tls == NULL)
	return;
    CBTF_Start(tls->EventSet);
}

void hwctime_collector_events_stop()
{
    /* Access our thread-local storage */
#ifdef USE_EXPLICIT_TLS
    TLS* tls = CBTF_GetTLS(TLSKey);
#else
    TLS* tls = &the_tls;
#endif
    if (hwctime_papi_init_done == 0 || tls == NULL)
	return;
    CBTF_Stop(tls->EventSet, NULL);
}
#endif
