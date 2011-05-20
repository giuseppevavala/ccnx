#include "common.h"

static int
ccn_stuff_interest(struct ccnr_handle * h,
				   struct fdholder * fdholder, struct ccn_charbuf * c);
static void
ccn_append_link_stuff(struct ccnr_handle * h,
					  struct fdholder * fdholder,
					  struct ccn_charbuf * c);


PUBLIC void
send_content(struct ccnr_handle *h, struct fdholder *fdholder, struct content_entry *content)
{
    int n, a, b, size;
    if ((fdholder->flags & CCN_FACE_NOSEND) != 0) {
        // XXX - should count this.
        return;
    }
    size = content->size;
    if (h->debug & 4)
        ccnr_debug_ccnb(h, __LINE__, "content_to", fdholder,
                        content->key, size);
    /* Excise the message-digest name component */
    n = content->ncomps;
    if (n < 2) abort();
    a = content->comps[n - 2];
    b = content->comps[n - 1];
    if (b - a != 36)
        abort(); /* strange digest length */
    stuff_and_send(h, fdholder, content->key, a, content->key + b, size - b);
    ccnr_meter_bump(h, fdholder->meter[FM_DATO], 1);
    h->content_items_sent += 1;
}

/**
 * Send a message in a PDU, possibly stuffing other interest messages into it.
 * The message may be in two pieces.
 */
PUBLIC void
stuff_and_send(struct ccnr_handle *h, struct fdholder *fdholder,
               const unsigned char *data1, size_t size1,
               const unsigned char *data2, size_t size2) {
    struct ccn_charbuf *c = NULL;
    
    if ((fdholder->flags & CCN_FACE_LINK) != 0) {
        c = charbuf_obtain(h);
        ccn_charbuf_reserve(c, size1 + size2 + 5 + 8);
        ccn_charbuf_append_tt(c, CCN_DTAG_CCNProtocolDataUnit, CCN_DTAG);
        ccn_charbuf_append(c, data1, size1);
        if (size2 != 0)
            ccn_charbuf_append(c, data2, size2);
        ccn_stuff_interest(h, fdholder, c);
        ccn_append_link_stuff(h, fdholder, c);
        ccn_charbuf_append_closer(c);
    }
    else if (size2 != 0 || 1 > size1 + size2 ||
             (fdholder->flags & (CCN_FACE_SEQOK | CCN_FACE_SEQPROBE)) != 0) {
        c = charbuf_obtain(h);
        ccn_charbuf_append(c, data1, size1);
        if (size2 != 0)
            ccn_charbuf_append(c, data2, size2);
        ccn_stuff_interest(h, fdholder, c);
        ccn_append_link_stuff(h, fdholder, c);
    }
    else {
        /* avoid a copy in this case */
        ccnr_send(h, fdholder, data1, size1);
        return;
    }
    ccnr_send(h, fdholder, c->buf, c->length);
    charbuf_release(h, c);
    return;
}

/**
 * Stuff a PDU with interest messages that will fit.
 *
 * Note by default stuffing does not happen due to the setting of h->mtu.
 * @returns the number of messages that were stuffed.
 */
static int
ccn_stuff_interest(struct ccnr_handle *h,
                   struct fdholder *fdholder, struct ccn_charbuf *c)
{
    int n_stuffed = 0;
    return(n_stuffed);
}

PUBLIC void
ccn_link_state_init(struct ccnr_handle *h, struct fdholder *fdholder)
{
    int checkflags;
    int matchflags;
    
    matchflags = CCN_FACE_DGRAM;
    checkflags = matchflags | CCN_FACE_MCAST | CCN_FACE_GG | CCN_FACE_SEQOK |                  CCN_FACE_PASSIVE;
    if ((fdholder->flags & checkflags) != matchflags)
        return;
    /* Send one sequence number to see if the other side wants to play. */
    fdholder->pktseq = nrand48(h->seed);
    fdholder->flags |= CCN_FACE_SEQPROBE;
}

static void
ccn_append_link_stuff(struct ccnr_handle *h,
                      struct fdholder *fdholder,
                      struct ccn_charbuf *c)
{
    if ((fdholder->flags & (CCN_FACE_SEQOK | CCN_FACE_SEQPROBE)) == 0)
        return;
    ccn_charbuf_append_tt(c, CCN_DTAG_SequenceNumber, CCN_DTAG);
    ccn_charbuf_append_tt(c, 2, CCN_BLOB);
    ccn_charbuf_append_value(c, fdholder->pktseq, 2);
    ccnb_element_end(c);
    if (0)
        ccnr_msg(h, "debug.%d pkt_to %u seq %u",
                 __LINE__, fdholder->filedesc, (unsigned)fdholder->pktseq);
    fdholder->pktseq++;
    fdholder->flags &= ~CCN_FACE_SEQPROBE;
}

PUBLIC int
process_incoming_link_message(struct ccnr_handle *h,
                              struct fdholder *fdholder, enum ccn_dtag dtag,
                              unsigned char *msg, size_t size)
{
    uintmax_t s;
    int checkflags;
    int matchflags;
    struct ccn_buf_decoder decoder;
    struct ccn_buf_decoder *d = ccn_buf_decoder_start(&decoder, msg, size);

    switch (dtag) {
        case CCN_DTAG_SequenceNumber:
            s = ccn_parse_required_tagged_binary_number(d, dtag, 1, 6);
            if (d->decoder.state < 0)
                return(d->decoder.state);
            /*
             * If the other side is unicast and sends sequence numbers,
             * then it is OK for us to send numbers as well.
             */
            matchflags = CCN_FACE_DGRAM;
            checkflags = matchflags | CCN_FACE_MCAST | CCN_FACE_SEQOK;
            if ((fdholder->flags & checkflags) == matchflags)
                fdholder->flags |= CCN_FACE_SEQOK;
            if (fdholder->rrun == 0) {
                fdholder->rseq = s;
                fdholder->rrun = 1;
                return(0);
            }
            if (s == fdholder->rseq + 1) {
                fdholder->rseq = s;
                if (fdholder->rrun < 255)
                    fdholder->rrun++;
                return(0);
            }
            if (s > fdholder->rseq && s - fdholder->rseq < 255) {
                ccnr_msg(h, "seq_gap %u %ju to %ju",
                         fdholder->filedesc, fdholder->rseq, s);
                fdholder->rseq = s;
                fdholder->rrun = 1;
                return(0);
            }
            if (s <= fdholder->rseq) {
                if (fdholder->rseq - s < fdholder->rrun) {
                    ccnr_msg(h, "seq_dup %u %ju", fdholder->filedesc, s);
                    return(0);
                }
                if (fdholder->rseq - s < 255) {
                    /* Received out of order */
                    ccnr_msg(h, "seq_ooo %u %ju", fdholder->filedesc, s);
                    if (s == fdholder->rseq - fdholder->rrun) {
                        fdholder->rrun++;
                        return(0);
                    }
                }
            }
            fdholder->rseq = s;
            fdholder->rrun = 1;
            break;
        default:
            return(-1);
    }
    return(0);
}
PUBLIC void
do_deferred_write(struct ccnr_handle *h, int fd)
{
    /* This only happens on connected sockets */
    ssize_t res;
    struct fdholder *fdholder = fdholder_from_fd(h, fd);
    if (fdholder == NULL)
        return;
    if (fdholder->outbuf != NULL) {
        ssize_t sendlen = fdholder->outbuf->length - fdholder->outbufindex;
        if (sendlen > 0) {
            res = send(fd, fdholder->outbuf->buf + fdholder->outbufindex, sendlen, 0);
            if (res == -1) {
                if (errno == EPIPE) {
                    fdholder->flags |= CCN_FACE_NOSEND;
                    fdholder->outbufindex = 0;
                    ccn_charbuf_destroy(&fdholder->outbuf);
                    return;
                }
                ccnr_msg(h, "send: %s (errno = %d)", strerror(errno), errno);
                shutdown_client_fd(h, fd);
                return;
            }
            if (res == sendlen) {
                fdholder->outbufindex = 0;
                ccn_charbuf_destroy(&fdholder->outbuf);
                if ((fdholder->flags & CCN_FACE_CLOSING) != 0)
                    shutdown_client_fd(h, fd);
                return;
            }
            fdholder->outbufindex += res;
            return;
        }
        fdholder->outbufindex = 0;
        ccn_charbuf_destroy(&fdholder->outbuf);
    }
    if ((fdholder->flags & CCN_FACE_CLOSING) != 0)
        shutdown_client_fd(h, fd);
    else if ((fdholder->flags & CCN_FACE_CONNECTING) != 0) {
        fdholder->flags &= ~CCN_FACE_CONNECTING;
        ccnr_face_status_change(h, fdholder->filedesc);
    }
    else
        ccnr_msg(h, "ccnr:do_deferred_write: something fishy on %d", fd);
}
