/*
    Copyright (c) 2010-2012 250bpm s.r.o.
    Copyright (c) 2010-2011 Other contributors as noted in the AUTHORS file

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

#include "io_thread.hpp"
#include "err.hpp"

#include "select.hpp"
#include "poll.hpp"
#include "epoll.hpp"
#include "devpoll.hpp"
#include "kqueue.hpp"

xs::io_thread_t *xs::io_thread_t::create (xs::ctx_t *ctx_, uint32_t tid_)
{
    io_thread_t *result;
#if defined XS_HAVE_SELECT
    result = new (std::nothrow) select_t (ctx_, tid_);
#elif defined XS_HAVE_POLL
    result = new (std::nothrow) poll_t (ctx_, tid_);
#elif defined XS_HAVE_EPOLL
    result = new (std::nothrow) epoll_t (ctx_, tid_);
#elif defined XS_HAVE_DEVPOLL
    result = new (std::nothrow) devpoll_t (ctx_, tid_);
#elif defined XS_HAVE_KQUEUE
    result = new (std::nothrow) kqueue_t (ctx_, tid_);
#endif
    alloc_assert (result);
    return result;
}

xs::io_thread_t::io_thread_t (xs::ctx_t *ctx_, uint32_t tid_) :
    object_t (ctx_, tid_)
{
    int rc = mailbox_init (&mailbox);
    errno_assert (rc == 0);
}

xs::io_thread_t::~io_thread_t ()
{
    mailbox_close (&mailbox);
}

void xs::io_thread_t::start ()
{
    mailbox_handle = add_fd (mailbox_fd (&mailbox), this);
    set_pollin (mailbox_handle);
    xstart ();
}

void xs::io_thread_t::stop ()
{
    //  Ask the I/O thread to stop.
    send_stop ();
}

void xs::io_thread_t::process_stop ()
{
    rm_fd (mailbox_handle);
    xstop ();
}

xs::mailbox_t *xs::io_thread_t::get_mailbox ()
{
    return &mailbox;
}

int xs::io_thread_t::get_load ()
{
    return load.get ();
}

void xs::io_thread_t::adjust_load (int amount_)
{
    if (amount_ > 0)
        load.add (amount_);
    else if (amount_ < 0)
        load.sub (-amount_);
}

xs::handle_t xs::io_thread_t::add_timer (int timeout_, i_poll_events *sink_)
{
    uint64_t expiration = clock.now_ms () + timeout_;
    timer_info_t info = {sink_, timers_t::iterator ()};
    timers_t::iterator it = timers.insert (
        timers_t::value_type (expiration, info));
    it->second.self = it;
    return (handle_t) &(it->second);
}

void xs::io_thread_t::rm_timer (handle_t handle_)
{
    timer_info_t *info = (timer_info_t*) handle_;
    timers.erase (info->self);
}

uint64_t xs::io_thread_t::execute_timers ()
{
    //  Fast track.
    if (timers.empty ())
        return 0;

    //  Get the current time.
    uint64_t current = clock.now_ms ();

    //   Execute the timers that are already due.
    timers_t::iterator it = timers.begin ();
    while (it != timers.end ()) {

        //  If we have to wait to execute the item, same will be true about
        //  all the following items (multimap is sorted). Thus we can stop
        //  checking the subsequent timers and return the time to wait for
        //  the next timer (at least 1ms).
        if (it->first > current)
            return it->first - current;

        //  Trigger the timer.
        it->second.sink->timer_event ((handle_t) &it->second);

        //  Remove it from the list of active timers.
        timers_t::iterator o = it;
        ++it;
        timers.erase (o);
    }

    //  There are no more timers.
    return 0;
}

void xs::io_thread_t::in_event (fd_t fd_)
{
    //  TODO: Do we want to limit number of commands I/O thread can
    //  process in a single go?

    while (true) {

        //  Get the next command. If there is none, exit.
        command_t cmd;
        int rc = mailbox_recv (&mailbox, &cmd, 0);
        if (rc != 0 && errno == EINTR)
            continue;
        if (rc != 0 && errno == EAGAIN)
            break;
        errno_assert (rc == 0);

        //  Process the command.
        cmd.destination->process_command (cmd);
    }
}

void xs::io_thread_t::out_event (fd_t fd_)
{
    //  We are never polling for POLLOUT here. This function is never called.
    xs_assert (false);
}

void xs::io_thread_t::timer_event (handle_t handle_)
{
    //  No timers here. This function is never called.
    xs_assert (false);
}
