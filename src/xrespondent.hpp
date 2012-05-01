/*
    Copyright (c) 2012 250bpm s.r.o.
    Copyright (c) 2012 Other contributors as noted in the AUTHORS file

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

#ifndef __XS_XRESPONDENT_HPP_INCLUDED__
#define __XS_XRESPONDENT_HPP_INCLUDED__

#include <map>

#include "socket_base.hpp"
#include "session_base.hpp"
#include "stdint.hpp"
#include "blob.hpp"
#include "msg.hpp"
#include "fq.hpp"

namespace xs
{

    class ctx_t;
    class pipe_t;
    class io_thread_t;

    //  TODO: This class uses O(n) scheduling. Rewrite it to use O(1) algorithm.
    class xrespondent_t :
        public socket_base_t
    {
    public:

        xrespondent_t (xs::ctx_t *parent_, uint32_t tid_, int sid_);
        ~xrespondent_t ();

        //  Overloads of functions from socket_base_t.
        void xattach_pipe (xs::pipe_t *pipe_, bool icanhasall_);
        int xsend (msg_t *msg_, int flags_);
        int xrecv (msg_t *msg_, int flags_);
        bool xhas_in ();
        bool xhas_out ();
        void xread_activated (xs::pipe_t *pipe_);
        void xwrite_activated (xs::pipe_t *pipe_);
        void xterminated (xs::pipe_t *pipe_);

    protected:

        //  Rollback any message parts that were sent but not yet flushed.
        int rollback ();

    private:

        //  Fair queueing object for inbound pipes.
        fq_t fq;

        //  This value is either 0 (nothing is prefetched), 1 (only message body
        //  is prefetched) or 2 (both identity and message body are prefetched).
        int prefetched;

        //  Holds the prefetched identity.
        blob_t prefetched_id;

        //  Holds the prefetched message.
        msg_t prefetched_msg;

        //  If true, more incoming message parts are expected.
        bool more_in;

        struct outpipe_t
        {
            xs::pipe_t *pipe;
            bool active;
        };

        //  Outbound pipes indexed by the peer IDs.
        typedef std::map <uint32_t, outpipe_t> outpipes_t;
        outpipes_t outpipes;

        //  The pipe we are currently writing to.
        xs::pipe_t *current_out;

        //  If true, more outgoing message parts are expected.
        bool more_out;

        //  Peer ID are generated. It's a simple increment and wrap-over
        //  algorithm. This value is the next ID to use (if not used already).
        uint32_t next_peer_id;

        xrespondent_t (const xrespondent_t&);
        const xrespondent_t &operator = (const xrespondent_t&);
    };

    class xrespondent_session_t : public session_base_t
    {
    public:

        xrespondent_session_t (xs::io_thread_t *io_thread_, bool connect_,
            socket_base_t *socket_, const options_t &options_,
            const char *protocol_, const char *address_);
        ~xrespondent_session_t ();

    private:

        xrespondent_session_t (const xrespondent_session_t&);
        const xrespondent_session_t &operator = (const xrespondent_session_t&);
    };

}

#endif
