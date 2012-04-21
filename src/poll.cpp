/*
    Copyright (c) 2009-2012 250bpm s.r.o.
    Copyright (c) 2007-2009 iMatix Corporation
    Copyright (c) 2007-2011 Other contributors as noted in the AUTHORS file

    This file is part of Crossroads I/O project.

    Crossroads I/O is free software; you can redistribute it and/or modify it
    under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Crossroads is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "poll.hpp"

#if defined XS_USE_ASYNC_POLL

#include <sys/types.h>
#include <sys/time.h>
#include <poll.h>
#include <algorithm>

#include "poll.hpp"
#include "err.hpp"
#include "config.hpp"

xs::poll_t::poll_t (xs::ctx_t *ctx_, uint32_t tid_) :
    io_thread_t (ctx_, tid_),
    retired (false),
    stopping (false)
{
}

xs::poll_t::~poll_t ()
{
    thread_stop (&worker);
}

xs::handle_t xs::poll_t::add_fd (fd_t fd_, i_poll_events *events_)
{
    //  If the file descriptor table is too small expand it.
    fd_table_t::size_type sz = fd_table.size ();
    if (sz <= (fd_table_t::size_type) fd_) {
        fd_table.resize (fd_ + 1);
        while (sz != (fd_table_t::size_type) (fd_ + 1)) {
            fd_table [sz].index = retired_fd;
            ++sz;
        }
    }

    pollfd pfd = {fd_, 0, 0};
    pollset.push_back (pfd);
    assert (fd_table [fd_].index == retired_fd);

    fd_table [fd_].index = pollset.size() - 1;
    fd_table [fd_].events = events_;

    //  Increase the load metric of the thread.
    adjust_load (1);

    return fdtoptr (fd_);
}

void xs::poll_t::rm_fd (handle_t handle_)
{
    int index = fd_table [ptrtofd (handle_)].index;
    assert (index != retired_fd);

    //  Mark the fd as unused.
    pollset [index].fd = retired_fd;
    fd_table [ptrtofd (handle_)].index = retired_fd;
    retired = true;

    //  Decrease the load metric of the thread.
    adjust_load (-1);
}

void xs::poll_t::set_pollin (handle_t handle_)
{
    int index = fd_table [ptrtofd (handle_)].index;
    pollset [index].events |= POLLIN;
}

void xs::poll_t::reset_pollin (handle_t handle_)
{
    int index = fd_table [ptrtofd (handle_)].index;
    pollset [index].events &= ~((short) POLLIN);
}

void xs::poll_t::set_pollout (handle_t handle_)
{
    int index = fd_table [ptrtofd (handle_)].index;
    pollset [index].events |= POLLOUT;
}

void xs::poll_t::reset_pollout (handle_t handle_)
{
    int index = fd_table [ptrtofd (handle_)].index;
    pollset [index].events &= ~((short) POLLOUT);
}

void xs::poll_t::xstart ()
{
    thread_start (&worker, worker_routine, this);
}

void xs::poll_t::xstop ()
{
    stopping = true;
}

void xs::poll_t::loop ()
{
    while (!stopping) {

        //  Execute any due timers.
        int timeout = (int) execute_timers ();

        //  Wait for events.
        int rc = poll (&pollset [0], pollset.size (), timeout ? timeout : -1);
        if (rc == -1 && errno == EINTR)
            continue;
        errno_assert (rc != -1);


        //  If there are no events (i.e. it's a timeout) there's no point
        //  in checking the pollset.
        if (rc == 0)
            continue;

        for (pollset_t::size_type i = 0; i != pollset.size (); i++) {

            xs_assert (!(pollset [i].revents & POLLNVAL));
            if (pollset [i].fd == retired_fd)
               continue;
            if (pollset [i].revents & (POLLERR | POLLHUP))
                fd_table [pollset [i].fd].events->in_event (pollset [i].fd);
            if (pollset [i].fd == retired_fd)
               continue;
            if (pollset [i].revents & POLLOUT)
                fd_table [pollset [i].fd].events->out_event (pollset [i].fd);
            if (pollset [i].fd == retired_fd)
               continue;
            if (pollset [i].revents & POLLIN)
                fd_table [pollset [i].fd].events->in_event (pollset [i].fd);
        }

        //  Clean up the pollset and update the fd_table accordingly.
        if (retired) {
            pollset_t::size_type i = 0;
            while (i < pollset.size ()) {
                if (pollset [i].fd == retired_fd)
                    pollset.erase (pollset.begin () + i);
                else {
                    fd_table [pollset [i].fd].index = i;
                    i ++;
                }
            }
            retired = false;
        }
    }
}

void xs::poll_t::worker_routine (void *arg_)
{
    ((poll_t*) arg_)->loop ();
}

#endif
