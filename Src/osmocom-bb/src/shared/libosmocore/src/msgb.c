/* (C) 2008 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
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


#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <osmocom/core/msgb.h>
//#include <openbsc/gsm_data.h>
#include <osmocom/core/talloc.h>
//#include <openbsc/debug.h>

void *tall_msgb_ctx;

struct msgb *msgb_alloc(uint16_t size, const char *name)
{
	struct msgb *msg;

	msg = _talloc_zero(tall_msgb_ctx, sizeof(*msg) + size, name);

	if (!msg) {
		//LOGP(DRSL, LOGL_FATAL, "unable to allocate msgb\n");
		return NULL;
	}

	msg->data_len = size;
	msg->len = 0;
	msg->data = msg->_data;
	msg->head = msg->_data;
	msg->tail = msg->_data;

	return msg;
}

void msgb_free(struct msgb *m)
{
	talloc_free(m);
}

void msgb_enqueue(struct llist_head *queue, struct msgb *msg)
{
	llist_add_tail(&msg->list, queue);
}

struct msgb *msgb_dequeue(struct llist_head *queue)
{
	struct llist_head *lh;

	if (llist_empty(queue))
		return NULL;

	lh = queue->next;
	llist_del(lh);
	
	return llist_entry(lh, struct msgb, list);
}

void msgb_reset(struct msgb *msg)
{
	msg->len = 0;
	msg->data = msg->_data;
	msg->head = msg->_data;
	msg->tail = msg->_data;

	msg->trx = NULL;
	msg->lchan = NULL;
	msg->l2h = NULL;
	msg->l3h = NULL;
	msg->l4h = NULL;

	memset(&msg->cb, 0, sizeof(msg->cb));
}

uint8_t *msgb_data(const struct msgb *msg)
{
	return msg->data;
}

uint16_t msgb_length(const struct msgb *msg)
{
	return msg->len;
}
