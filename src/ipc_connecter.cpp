/*
    Copyright (c) 2011-2012 250bpm s.r.o.
    Copyright (c) 2011 Other contributors as noted in the AUTHORS file

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

#include "ipc_connecter.hpp"

#if !defined XS_HAVE_WINDOWS && !defined XS_HAVE_OPENVMS

#include <new>
#include <string>

#include "stream_engine.hpp"
#include "io_thread.hpp"
#include "platform.hpp"
#include "random.hpp"
#include "err.hpp"
#include "ip.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

xs::ipc_connecter_t::ipc_connecter_t (class io_thread_t *io_thread_,
      class session_base_t *session_, const options_t &options_,
      const char *address_, bool wait_) :
    own_t (io_thread_, options_),
    io_object_t (io_thread_),
    s (retired_fd),
    handle (NULL),
    wait (wait_),
    session (session_),
    current_reconnect_ivl(options.reconnect_ivl),
    reconnect_timer (NULL)
{
    //  TODO: set_addess should be called separately, so that the error
    //  can be propagated.
    int rc = set_address (address_);
    xs_assert (rc == 0);
}

xs::ipc_connecter_t::~ipc_connecter_t ()
{
    if (wait) {
        xs_assert (reconnect_timer);
        rm_timer (reconnect_timer);
    }
    if (handle) {
        rm_fd (handle);
        handle = NULL;
    }

    close ();
}

void xs::ipc_connecter_t::process_plug ()
{
    if (wait)
        add_reconnect_timer();
    else
        start_connecting ();
}

void xs::ipc_connecter_t::in_event (fd_t fd_)
{
    //  We are not polling for incomming data, so we are actually called
    //  because of error here. However, we can get error on out event as well
    //  on some platforms, so we'll simply handle both events in the same way.
    out_event (fd_);
}

void xs::ipc_connecter_t::out_event (fd_t fd_)
{
    fd_t fd = connect ();
    xs_assert (handle);
    rm_fd (handle);
    handle = NULL;

    //  Handle the error condition by attempt to reconnect.
    if (fd == retired_fd) {
        close ();
        wait = true;
        add_reconnect_timer();
        return;
    }

    //  Create the engine object for this connection.
    stream_engine_t *engine = new (std::nothrow) stream_engine_t (fd, options);
    alloc_assert (engine);

    //  Attach the engine to the corresponding session object.
    send_attach (session, engine);

    //  Shut the connecter down.
    terminate ();
}

void xs::ipc_connecter_t::timer_event (handle_t handle_)
{
    xs_assert (handle_ == reconnect_timer);
    reconnect_timer = NULL;
    wait = false;
    start_connecting ();
}

void xs::ipc_connecter_t::start_connecting ()
{
    //  Open the connecting socket.
    int rc = open ();

    //  Connect may succeed in synchronous manner.
    if (rc == 0) {
        xs_assert (!handle);
        handle = add_fd (s);
        out_event (s);
        return;
    }

    //  Connection establishment may be delayed. Poll for its completion.
    else if (rc == -1 && errno == EAGAIN) {
        xs_assert (!handle);
        handle = add_fd (s);
        set_pollout (handle);
        return;
    }

    //  Handle any other error condition by eventual reconnect.
    close ();
    wait = true;
    add_reconnect_timer();
}

void xs::ipc_connecter_t::add_reconnect_timer()
{
    xs_assert (reconnect_timer == NULL);
    reconnect_timer = add_timer (get_new_reconnect_ivl());
}

int xs::ipc_connecter_t::get_new_reconnect_ivl ()
{
    //  The new interval is the current interval + random value.
    int this_interval = current_reconnect_ivl +
        (generate_random () % options.reconnect_ivl);

    //  Only change the current reconnect interval  if the maximum reconnect
    //  interval was set and if it's larger than the reconnect interval.
    if (options.reconnect_ivl_max > 0 && 
        options.reconnect_ivl_max > options.reconnect_ivl) {

        //  Calculate the next interval
        current_reconnect_ivl = current_reconnect_ivl * 2;
        if(current_reconnect_ivl >= options.reconnect_ivl_max) {
            current_reconnect_ivl = options.reconnect_ivl_max;
        }   
    }
    return this_interval;
}

int xs::ipc_connecter_t::set_address (const char *addr_)
{
    return address.resolve (addr_);
}

int xs::ipc_connecter_t::open ()
{
    xs_assert (s == retired_fd);

    //  Create the socket.
    s = open_socket (AF_UNIX, SOCK_STREAM, 0);
    if (s == -1)
        return -1;

    //  Set the non-blocking flag.
    unblock_socket (s);

    //  Connect to the remote peer.
    int rc = ::connect (s, address.addr (), address.addrlen ());

    //  Connect was successfull immediately.
    if (rc == 0)
        return 0;

    //  Asynchronous connect was launched.
    if (rc == -1 && errno == EINPROGRESS) {
        errno = EAGAIN;
        return -1;
    }

    //  Forward the error.
    return -1;
}

int xs::ipc_connecter_t::close ()
{
    if (s == retired_fd)
        return 0;

    int rc = ::close (s);
    if (rc != 0)
        return -1;
    s = retired_fd;
    return 0;
}

xs::fd_t xs::ipc_connecter_t::connect ()
{
    //  Following code should handle both Berkeley-derived socket
    //  implementations and Solaris.
    int err = 0;
#if defined XS_HAVE_HPUX
    int len = sizeof (err);
#else
    socklen_t len = sizeof (err);
#endif
    int rc = getsockopt (s, SOL_SOCKET, SO_ERROR, (char*) &err, &len);
    if (rc == -1)
        err = errno;
    if (err != 0) {

        //  Assert if the error was caused by Crossroads bug.
        //  Networking problems are OK. No need to assert.
        errno = err;
        errno_assert (errno == ECONNREFUSED || errno == ECONNRESET ||
            errno == ETIMEDOUT || errno == EHOSTUNREACH ||
            errno == ENETUNREACH || errno == ENETDOWN);

        return retired_fd;
    }

    fd_t result = s;
    s = retired_fd;
    return result;
}

#endif

