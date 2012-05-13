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

#ifndef __XS_POLL_HPP_INCLUDED__
#define __XS_POLL_HPP_INCLUDED__

#include "polling.hpp"

#if defined XS_USE_ASYNC_POLL

#include <stddef.h>

#include <vector>

#include "fd.hpp"
#include "thread.hpp"
#include "io_thread.hpp"

namespace xs
{

    struct i_poll_events;

    //  Implements socket polling mechanism using the POSIX.1-2001
    //  poll() system call.

    class poll_t : public io_thread_t
    {
    public:

        poll_t (xs::ctx_t *ctx_, uint32_t tid_);
        ~poll_t ();

        //  Implementation of virtual functions from io_thread_t.
        handle_t add_fd (fd_t fd_, xs::i_poll_events *events_);
        void rm_fd (handle_t handle_);
        void set_pollin (handle_t handle_);
        void reset_pollin (handle_t handle_);
        void set_pollout (handle_t handle_);
        void reset_pollout (handle_t handle_);
        void xstart ();
        void xstop ();

    private:

        //  Main worker thread routine.
        static void worker_routine (void *arg_);

        //  Main event loop.
        void loop ();

        struct fd_entry_t
        {
            int index;
            xs::i_poll_events *events;
        };

        //  This table stores data for registered descriptors.
        typedef std::vector <fd_entry_t> fd_table_t;
        fd_table_t fd_table;

        //  Pollset to pass to the poll function.
        typedef std::vector <pollfd> pollset_t;
        pollset_t pollset;

        //  If true, there's at least one retired event source.
        bool retired;

        //  If true, thread is in the process of shutting down.
        bool stopping;

        //  Handle of the physical thread doing the I/O work.
        thread_t worker;

        poll_t (const poll_t&);
        const poll_t &operator = (const poll_t&);
    };

}

#endif

#endif
