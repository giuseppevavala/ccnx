/**
 * @file ccnr_msg.c
 *
 * Logging support for ccnr.
 * 
 * Part of ccnr -  CCNx Repository Daemon.
 *
 */

/*
 * Copyright (C) 2008, 2009, 2011 Palo Alto Research Center, Inc.
 *
 * This work is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 * This work is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
 
#include <stdio.h>
#include <sys/time.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include <ccn/ccn.h>
#include <ccn/charbuf.h>
#include <ccn/uri.h>

#include "ccnr_private.h"

#include "ccnr_msg.h"

/**
 *  Produce ccnr debug output.
 *  Output is produced via h->logger under the control of h->debug;
 *  prepends decimal timestamp and process identification.
 *  Caller should not supply newlines.
 *  @param      h  the ccnr handle
 *  @param      fmt  printf-like format string
 */
void
ccnr_msg(struct ccnr_handle *h, const char *fmt, ...)
{
    struct timeval t;
    va_list ap;
    struct ccn_charbuf *b;
    int res;
    const char *portstr;    
    if (h == NULL || h->debug == 0 || h->logger == 0)
        return;
    b = ccn_charbuf_create();
    if (b == NULL)
        return;
    gettimeofday(&t, NULL);
    if (((h->debug & 64) != 0) &&
        ((h->logbreak-- < 0 && t.tv_sec != h->logtime) ||
          t.tv_sec >= h->logtime + 30)) {
        portstr = h->portstr;
        if (portstr == NULL)
            portstr = "";
        ccn_charbuf_putf(b, "%ld.000000 ccnr[%d]: %s ____________________ %s",
                         (long)t.tv_sec, h->logpid, h->portstr, ctime(&t.tv_sec));
        h->logtime = t.tv_sec;
        h->logbreak = 30;
    }
    ccn_charbuf_putf(b, "%ld.%06u ccnr[%d]: %s\n",
        (long)t.tv_sec, (unsigned)t.tv_usec, h->logpid, fmt);
    va_start(ap, fmt);
    res = (*h->logger)(h->loggerdata, ccn_charbuf_as_string(b), ap);
    va_end(ap);
    ccn_charbuf_destroy(&b);
    /* if there's no one to hear, don't make a sound */
    if (res < 0)
        h->debug = 0;
}

/**
 *  Produce a ccnr debug trace entry.
 *  Output is produced by calling ccnr_msg.
 *  @param      h  the ccnr handle
 *  @param      lineno  caller's source line number (usually __LINE__)
 *  @param      msg  a short text tag to identify the entry
 *  @param      fdholder    handle of associated fdholder; may be NULL
 *  @param      ccnb    points to ccnb-encoded Interest or ContentObject
 *  @param      ccnb_size   is in bytes
 */
void
ccnr_debug_ccnb(struct ccnr_handle *h,
                int lineno,
                const char *msg,
                struct fdholder *fdholder,
                const unsigned char *ccnb,
                size_t ccnb_size)
{
    struct ccn_charbuf *c;
    struct ccn_parsed_interest pi;
    const unsigned char *nonce = NULL;
    size_t nonce_size = 0;
    size_t i;
    
    
    if (h != NULL && h->debug == 0)
        return;
    c = ccn_charbuf_create();
    ccn_charbuf_putf(c, "debug.%d %s ", lineno, msg);
    if (fdholder != NULL)
        ccn_charbuf_putf(c, "%u ", fdholder->filedesc);
    ccn_uri_append(c, ccnb, ccnb_size, 1);
    ccn_charbuf_putf(c, " (%u bytes)", (unsigned)ccnb_size);
    if (ccn_parse_interest(ccnb, ccnb_size, &pi, NULL) >= 0) {
        const char *p = "";
        ccn_ref_tagged_BLOB(CCN_DTAG_Nonce, ccnb,
                  pi.offset[CCN_PI_B_Nonce],
                  pi.offset[CCN_PI_E_Nonce],
                  &nonce,
                  &nonce_size);
        if (nonce_size > 0) {
            ccn_charbuf_putf(c, " ");
            if (nonce_size == 12)
                p = "CCC-P-F-T-NN";
            for (i = 0; i < nonce_size; i++)
                ccn_charbuf_putf(c, "%s%02X", (*p) && (*p++)=='-' ? "-" : "", nonce[i]);
        }
    }
    ccnr_msg(h, "%s", ccn_charbuf_as_string(c));
    ccn_charbuf_destroy(&c);
}

/**
 * CCNR Usage message
 */
const char *ccnr_usage_message =
    "ccnr - CCNx Repository Daemon\n"
    "  options: none\n"
    "  arguments: none\n"
    "  environment variables:\n"
    "    CCNR_DEBUG=\n"
    "      0 - no messages\n"
    "      1 - basic messages (any non-zero value gets these)\n"
    "      2 - interest messages\n"
    "      4 - content messages\n"
    "      8 - matching details\n"
    "      16 - interest details\n"
    "      32 - gory interest details\n"
    "      64 - log occasional human-readable timestamps\n"
    "      128 - fdholder registration debugging\n"
    "      256 - shut down if ccnd goes away\n"
    "      bitwise OR these together for combinations; -1 gets max logging\n"
    "    CCNR_DIRECTORY=\n"
    "      Directory where ccnr data is kept\n"
    "      Defaults to current directory\n"
    "    CCNR_GLOBAL_PREFIX=\n"
    "      CCNx URI representing the prefix where data/policy.xml is stored.\n"
    "      Only meaningful if no policy file exists at startup.\n"
    "      Defaults to ccnx:/parc.com/csl/ccn/Repos\n"
    "    CCNR_STATUS_PORT=\n"
    "      Port to use for status server.  Default is to not serve status.\n"
    "    CCNR_LISTEN_ON=\n"
    "      List of ip addresses to listen on for status; defaults to wildcard\n"
    ;
