/*
    Copyright (c) 2009-2012 250bpm s.r.o.
    Copyright (c) 2007-2009 iMatix Corporation
    Copyright (c) 2011 VMware, Inc.
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

#ifndef __XS_REQ_HPP_INCLUDED__
#define __XS_REQ_HPP_INCLUDED__

#include "xreq.hpp"
#include "stdint.hpp"

namespace xs
{

    class ctx_t;
    class msg_t;
    class io_thread_t;
    class socket_base_t;

    class req_t : public xreq_t
    {
    public:

        req_t (xs::ctx_t *parent_, uint32_t tid_, int sid_);
        ~req_t ();

        //  Overloads of functions from socket_base_t.
        int xsend (xs::msg_t *msg_, int flags_);
        int xrecv (xs::msg_t *msg_, int flags_);
        bool xhas_in ();
        bool xhas_out ();

    private:

        //  If true, request was already sent and reply wasn't received yet or
        //  was raceived partially.
        bool receiving_reply;

        //  If true, we are starting to send/recv a message. The first part
        //  of the message must be empty message part (backtrace stack bottom).
        bool message_begins;

        req_t (const req_t&);
        const req_t &operator = (const req_t&);
    };

    class req_session_t : public xreq_session_t
    {
    public:

        req_session_t (xs::io_thread_t *io_thread_, bool connect_,
            xs::socket_base_t *socket_, const options_t &options_,
            const char *protocol_, const char *address_);
        ~req_session_t ();

        //  Overloads of the functions from session_base_t.
        int write (msg_t *msg_);
        void detach ();

    private:

        enum {
            identity,
            bottom,
            body
        } state;

        req_session_t (const req_session_t&);
        const req_session_t &operator = (const req_session_t&);
    };

}

#endif
