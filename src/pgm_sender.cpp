/*
    Copyright (c) 2009-2012 250bpm s.r.o.
    Copyright (c) 2007-2009 iMatix Corporation
    Copyright (c) 2012 Lucina & Associates
    Copyright (c) 2010-2011 Miru Limited
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

#include "platform.hpp"

#if defined XS_HAVE_OPENPGM

#ifdef XS_HAVE_WINDOWS
#include "windows.hpp"
#endif

#include <stdlib.h>

#include "pgm_sender.hpp"
#include "session_base.hpp"
#include "err.hpp"
#include "wire.hpp"
#include "stdint.hpp"

xs::pgm_sender_t::pgm_sender_t (io_thread_t *parent_, 
      const options_t &options_) :
    io_object_t (parent_),
    encoder (0),
    pgm_socket (false, options_),
    options (options_),
    out_buffer (NULL),
    out_buffer_size (0),
    write_size (0),
    rx_timer (NULL),
    tx_timer (NULL)
{
}

int xs::pgm_sender_t::init (bool udp_encapsulation_, const char *network_)
{
    int rc = pgm_socket.init (udp_encapsulation_, network_);
    if (rc != 0)
        return rc;

    out_buffer_size = pgm_socket.get_max_tsdu_size ();
    out_buffer = (unsigned char*) malloc (out_buffer_size);
    alloc_assert (out_buffer);
    encode_buffer = out_buffer;
    encode_buffer_size = out_buffer_size;
    header_size = 0;

    //  If not using a legacy protocol, fill in our datagram header and reserve
    //  space for it in the datagram.
    if (!options.legacy_protocol) {
        sp_get_header (out_buffer, options.sp_service, options.sp_pattern,
            options.sp_version, options.sp_role);
        encode_buffer += SP_HEADER_LENGTH;
        encode_buffer_size -= SP_HEADER_LENGTH;
        header_size += SP_HEADER_LENGTH;
    }

    //  Reserve space in the datagram for the offset of the first message.
    offset_p = encode_buffer;
    encode_buffer += sizeof (uint16_t);
    encode_buffer_size -= sizeof (uint16_t);
    header_size += sizeof (uint16_t);

    return rc;
}

void xs::pgm_sender_t::plug (io_thread_t *io_thread_,
    session_base_t *session_)
{
    //  Alocate 2 fds for PGM socket.
    fd_t downlink_socket_fd = retired_fd;
    fd_t uplink_socket_fd = retired_fd;
    fd_t rdata_notify_fd = retired_fd;
    fd_t pending_notify_fd = retired_fd;

    encoder.set_session (session_);

    //  Fill fds from PGM transport and add them to the I/O thread.
    pgm_socket.get_sender_fds (&downlink_socket_fd, &uplink_socket_fd,
        &rdata_notify_fd, &pending_notify_fd);

    handle = add_fd (downlink_socket_fd);
    uplink_handle = add_fd (uplink_socket_fd);
    rdata_notify_handle = add_fd (rdata_notify_fd);   
    pending_notify_handle = add_fd (pending_notify_fd);

    //  Set POLLIN. We wont never want to stop polling for uplink = we never
    //  want to stop porocess NAKs.
    set_pollin (uplink_handle);
    set_pollin (rdata_notify_handle);
    set_pollin (pending_notify_handle);

    //  Set POLLOUT for downlink_socket_handle.
    set_pollout (handle);
}

void xs::pgm_sender_t::unplug ()
{
    if (rx_timer) {
        rm_timer (rx_timer);
        rx_timer = NULL;
    }

    if (tx_timer) {
        rm_timer (tx_timer);
        tx_timer = NULL;
    }

    rm_fd (handle);
    rm_fd (uplink_handle);
    rm_fd (rdata_notify_handle);
    rm_fd (pending_notify_handle);
    encoder.set_session (NULL);
}

void xs::pgm_sender_t::terminate ()
{
    unplug ();
    delete this;
}

void xs::pgm_sender_t::activate_out ()
{
    set_pollout (handle);
    out_event (retired_fd);
}

void xs::pgm_sender_t::activate_in ()
{
    xs_assert (false);
}

xs::pgm_sender_t::~pgm_sender_t ()
{
    if (out_buffer) {
        free (out_buffer);
        out_buffer = NULL;
    }
}

void xs::pgm_sender_t::in_event (fd_t fd_)
{
    if (rx_timer) {
        rm_timer (rx_timer);
        rx_timer = NULL;
    }

    //  In-event on sender side means NAK or SPMR receiving from some peer.
    pgm_socket.process_upstream ();
    if (errno == ENOMEM || errno == EBUSY) {
        const long timeout = pgm_socket.get_rx_timeout ();
        xs_assert (!rx_timer);
        rx_timer = add_timer (timeout);
    }
}

void xs::pgm_sender_t::out_event (fd_t fd_)
{
    //  POLLOUT event from send socket. If write buffer is empty (which means
    //  that the last write succeeded), try to read new data from the encoder.
    if (write_size == 0) {

        //  Pass our own buffer to the get_data () function to prevent it from
        //  returning its own buffer.
        int offset = -1;
        size_t data_size = encode_buffer_size;
        encoder.get_data (&encode_buffer, &data_size, &offset);

        //  If there are no data to write stop polling for output.
        if (!data_size) {
            reset_pollout (handle);
            return;
        }

        //  Put offset information in the buffer.
        write_size = header_size + data_size;
        put_uint16 (offset_p, offset == -1 ? 0xffff : (uint16_t) offset);
    }

    if (tx_timer) {
        rm_timer (tx_timer);
        tx_timer = NULL;
    }

    //  Send the data.
    size_t nbytes = pgm_socket.send (out_buffer, write_size);

    //  We can write either all data or 0 which means rate limit reached.
    if (nbytes == write_size) {
        write_size = 0;
    } else {
        xs_assert (nbytes == 0);

        if (errno == ENOMEM) {
            const long timeout = pgm_socket.get_tx_timeout ();
            xs_assert (!tx_timer);
            tx_timer = add_timer (timeout);
        } else
            errno_assert (errno == EBUSY);
    }
}

void xs::pgm_sender_t::timer_event (handle_t handle_)
{
    //  Timer cancels on return by io_thread.
    if (handle_ == rx_timer) {
        rx_timer = NULL;
        in_event (retired_fd);
    } else if (handle_ == tx_timer) {
        tx_timer = NULL;
        out_event (retired_fd);
    } else
        xs_assert (false);
}

#endif

