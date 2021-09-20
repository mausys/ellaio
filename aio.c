/*
 *
 *  Embedded Linux library
 *
 *  Copyright (C) 2011-2014  Intel Corporation. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <errno.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <errno.h>


#include <libaio.h>

#include "private.h"
#include "io.h"
#include "log.h"
#include "aio.h"

struct l_aio_request {
  struct iocb iocb;
  l_aio_cb_t cb;
  void *user_data;
};

struct l_aio {
  io_context_t ctx;
  struct l_io *io;
};

bool event_callback(struct l_io *io, void *user_data)
{
  struct l_aio * aio = user_data;

  static struct timespec timeout = { 0, 0 };
  struct io_event event;
  for (;;) {
    int r = io_getevents(aio->ctx, 0, 1, &event, &timeout);

    if (r != 1)
      break;

    struct l_aio_request * req = event.data;

    ssize_t result = event.res2;
    if (result >= 0)
      result = event.res;
    req->cb(result, req->user_data);
    l_free(req);
  }
  return true;
}

struct l_aio * l_aio_create(int maxevents)
{
  struct l_aio *aio = l_new(struct l_aio, 1);
  long r = io_setup(maxevents, &aio->ctx);

  if (r < 0)
    goto error_init;

  int evfd = eventfd(0, O_NONBLOCK | O_CLOEXEC);

  if (evfd < 0)
    goto error_event;

  aio->io = l_io_new(evfd);

  if (!l_io_set_read_handler(aio->io, event_callback, aio, NULL))
    goto error_handler;

  return aio;

error_handler:
  l_free(aio->io);
error_event:
  io_destroy(aio->ctx);
error_init:
  l_free(aio);
  return NULL;
}


int l_aio_read(struct l_aio *aio, l_aio_cb_t read_cb, int fd, long long offset,
               void *buffer, size_t count, void *user_data)
{
  struct l_aio_request *req = l_new(struct l_aio_request, 1);
  req->cb = read_cb;
  req->user_data = user_data;

  struct iocb *iocb = &req->iocb;

  struct iocb *iocbv[] = { iocb };

  io_prep_pread(iocb, fd, buffer, count, offset);
  io_set_eventfd(iocb, l_io_get_fd(aio->io));
  iocb->data = req;

  return io_submit(aio->ctx, 1, iocbv);
}
