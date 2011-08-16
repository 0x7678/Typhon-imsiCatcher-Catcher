/* GSMTAP support code in libmsomcore */
/*
 * (C) 2010-2011 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "../config.h"

#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/socket.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/rsl.h>

#include <sys/types.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

uint8_t chantype_rsl2gsmtap(uint8_t rsl_chantype, uint8_t link_id)
{
	uint8_t ret = GSMTAP_CHANNEL_UNKNOWN;

	switch (rsl_chantype) {
	case RSL_CHAN_Bm_ACCHs:
		ret = GSMTAP_CHANNEL_TCH_F;
		break;
	case RSL_CHAN_Lm_ACCHs:
		ret = GSMTAP_CHANNEL_TCH_H;
		break;
	case RSL_CHAN_SDCCH4_ACCH:
		ret = GSMTAP_CHANNEL_SDCCH4;
		break;
	case RSL_CHAN_SDCCH8_ACCH:
		ret = GSMTAP_CHANNEL_SDCCH8;
		break;
	case RSL_CHAN_BCCH:
		ret = GSMTAP_CHANNEL_BCCH;
		break;
	case RSL_CHAN_RACH:
		ret = GSMTAP_CHANNEL_RACH;
		break;
	case RSL_CHAN_PCH_AGCH:
		/* it could also be AGCH... */
		ret = GSMTAP_CHANNEL_PCH;
		break;
	}

	if (link_id & 0x40)
		ret |= GSMTAP_CHANNEL_ACCH;

	return ret;
}

/* receive a message from L1/L2 and put it in GSMTAP */
struct msgb *gsmtap_makemsg(uint16_t arfcn, uint8_t ts, uint8_t chan_type,
			    uint8_t ss, uint32_t fn, int8_t signal_dbm,
			    uint8_t snr, const uint8_t *data, unsigned int len)
{
	struct msgb *msg;
	struct gsmtap_hdr *gh;
	uint8_t *dst;

	msg = msgb_alloc(sizeof(*gh) + len, "gsmtap_tx");
	if (!msg)
		return NULL;

	gh = (struct gsmtap_hdr *) msgb_put(msg, sizeof(*gh));

	gh->version = GSMTAP_VERSION;
	gh->hdr_len = sizeof(*gh)/4;
	gh->type = GSMTAP_TYPE_UM;
	gh->timeslot = ts;
	gh->sub_slot = ss;
	gh->arfcn = htons(arfcn);
	gh->snr_db = snr;
	gh->signal_dbm = signal_dbm;
	gh->frame_number = htonl(fn);
	gh->sub_type = chan_type;
	gh->antenna_nr = 0;

	dst = msgb_put(msg, len);
	memcpy(dst, data, len);

	return msg;
}

#ifdef HAVE_SYS_SOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>

/* Open a GSMTAP source (sending) socket, conncet it to host/port and
 * return resulting fd */
int gsmtap_source_init_fd(const char *host, uint16_t port)
{
	if (port == 0)
		port = GSMTAP_UDP_PORT;
	if (host == NULL)
		host = "localhost";

	return osmo_sock_init(AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP, host, port,
				OSMO_SOCK_F_CONNECT);
}

int gsmtap_source_add_sink_fd(int gsmtap_fd)
{
	struct sockaddr_storage ss;
	socklen_t ss_len = sizeof(ss);
	int rc;

	rc = getpeername(gsmtap_fd, (struct sockaddr *)&ss, &ss_len);
	if (rc < 0)
		return rc;

	if (osmo_sockaddr_is_local((struct sockaddr *)&ss, ss_len) == 1) {
		rc = osmo_sock_init_sa((struct sockaddr *)&ss, SOCK_DGRAM,
					IPPROTO_UDP, OSMO_SOCK_F_BIND);
		if (rc >= 0)
			return rc;
	}

	return -ENODEV;
}

int gsmtap_sendmsg(struct gsmtap_inst *gti, struct msgb *msg)
{
	if (!gti)
		return -ENODEV;

	if (gti->ofd_wq_mode)
		return osmo_wqueue_enqueue(&gti->wq, msg);
	else {
		/* try immediate send and return error if any */
		int rc;

		rc = write(gsmtap_inst_fd(gti), msg->data, msg->len);
		if (rc <= 0) {
			return rc;
		} else if (rc >= msg->len) {
			msgb_free(msg);
			return 0;
		} else {
			/* short write */
			return -EIO;
		}
	}
}

/* receive a message from L1/L2 and put it in GSMTAP */
int gsmtap_send(struct gsmtap_inst *gti, uint16_t arfcn, uint8_t ts,
		uint8_t chan_type, uint8_t ss, uint32_t fn,
		int8_t signal_dbm, uint8_t snr, const uint8_t *data,
		unsigned int len)
{
	struct msgb *msg;

	if (!gti)
		return -ENODEV;

	msg = gsmtap_makemsg(arfcn, ts, chan_type, ss, fn, signal_dbm,
			     snr, data, len);
	if (!msg)
		return -ENOMEM;

	return gsmtap_sendmsg(gti, msg);
}

/* Callback from select layer if we can write to the socket */
static int gsmtap_wq_w_cb(struct osmo_fd *ofd, struct msgb *msg)
{
	int rc;

	rc = write(ofd->fd, msg->data, msg->len);
	if (rc < 0) {
		perror("writing msgb to gsmtap fd");
		return rc;
	}
	if (rc != msg->len) {
		perror("short write to gsmtap fd");
		return -EIO;
	}

	return 0;
}

/* Callback from select layer if we can read from the sink socket */
static int gsmtap_sink_fd_cb(struct osmo_fd *fd, unsigned int flags)
{
	int rc;
	uint8_t buf[4096];

	if (!(flags & BSC_FD_READ))
		return 0;

	rc = read(fd->fd, buf, sizeof(buf));
	if (rc < 0) {
		perror("reading from gsmtap sink fd");
		return rc;
	}
	/* simply discard any data arriving on the socket */

	return 0;
}

/* Add a local sink to an existing GSMTAP source instance */
int gsmtap_source_add_sink(struct gsmtap_inst *gti)
{
	int fd;

	fd = gsmtap_source_add_sink_fd(gsmtap_inst_fd(gti));
	if (fd < 0)
		return fd;

	if (gti->ofd_wq_mode) {
		struct osmo_fd *sink_ofd;

		sink_ofd = &gti->sink_ofd;
		sink_ofd->fd = fd;
		sink_ofd->when = BSC_FD_READ;
		sink_ofd->cb = gsmtap_sink_fd_cb;

		osmo_fd_register(sink_ofd);
	}

	return fd;
}

/* like gsmtap_init2() but integrated with libosmocore select.c */
struct gsmtap_inst *gsmtap_source_init(const char *host, uint16_t port,
					int ofd_wq_mode)
{
	struct gsmtap_inst *gti;
	int fd;

	fd = gsmtap_source_init_fd(host, port);
	if (fd < 0)
		return NULL;

	gti = talloc_zero(NULL, struct gsmtap_inst);
	gti->ofd_wq_mode = ofd_wq_mode;
	gti->wq.bfd.fd = fd;
	gti->sink_ofd.fd = -1;

	if (ofd_wq_mode) {
		osmo_wqueue_init(&gti->wq, 64);
		gti->wq.write_cb = &gsmtap_wq_w_cb;

		osmo_fd_register(&gti->wq.bfd);
	}

	return gti;
}

#endif /* HAVE_SYS_SOCKET_H */
