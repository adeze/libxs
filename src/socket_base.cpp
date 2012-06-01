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

#include <new>
#include <string>
#include <algorithm>

#include "platform.hpp"

#if defined XS_HAVE_WINDOWS
#include "windows.hpp"
#if defined _MSC_VER
#include <intrin.h>
#endif
#else
#include <unistd.h>
#endif
#ifdef XS_HAVE_OPENPGM
#include "pgm_socket.hpp"
#endif

#include "socket_base.hpp"
#include "tcp_listener.hpp"
#include "ipc_listener.hpp"
#include "tcp_connecter.hpp"
#include "ipc_connecter.hpp"
#include "io_thread.hpp"
#include "session_base.hpp"
#include "config.hpp"
#include "pipe.hpp"
#include "err.hpp"
#include "ctx.hpp"
#include "platform.hpp"
#include "likely.hpp"
#include "msg.hpp"

#include "pair.hpp"
#include "pub.hpp"
#include "sub.hpp"
#include "req.hpp"
#include "rep.hpp"
#include "pull.hpp"
#include "push.hpp"
#include "xreq.hpp"
#include "xrep.hpp"
#include "xpub.hpp"
#include "xsub.hpp"
#include "surveyor.hpp"
#include "xsurveyor.hpp"
#include "respondent.hpp"
#include "xrespondent.hpp"

bool xs::socket_base_t::check_tag ()
{
    return tag == 0xbaddecaf;
}

xs::socket_base_t *xs::socket_base_t::create (int type_, class ctx_t *parent_,
    uint32_t tid_, int sid_)
{
    socket_base_t *s = NULL;
    switch (type_) {

    case XS_PAIR:
        s = new (std::nothrow) pair_t (parent_, tid_, sid_);
        break;
    case XS_PUB:
        s = new (std::nothrow) pub_t (parent_, tid_, sid_);
        break;
    case XS_SUB:
        s = new (std::nothrow) sub_t (parent_, tid_, sid_);
        break;
    case XS_REQ:
        s = new (std::nothrow) req_t (parent_, tid_, sid_);
        break;
    case XS_REP:
        s = new (std::nothrow) rep_t (parent_, tid_, sid_);
        break;
    case XS_XREQ:
        s = new (std::nothrow) xreq_t (parent_, tid_, sid_);
        break;
    case XS_XREP:
        s = new (std::nothrow) xrep_t (parent_, tid_, sid_);
        break;     
    case XS_PULL:
        s = new (std::nothrow) pull_t (parent_, tid_, sid_);
        break;
    case XS_PUSH:
        s = new (std::nothrow) push_t (parent_, tid_, sid_);
        break;
    case XS_XPUB:
        s = new (std::nothrow) xpub_t (parent_, tid_, sid_);
        break;
    case XS_XSUB:
        s = new (std::nothrow) xsub_t (parent_, tid_, sid_);
        break;
    case XS_SURVEYOR:
        s = new (std::nothrow) surveyor_t (parent_, tid_, sid_);
        break;
    case XS_XSURVEYOR:
        s = new (std::nothrow) xsurveyor_t (parent_, tid_, sid_);
        break;
    case XS_RESPONDENT:
        s = new (std::nothrow) respondent_t (parent_, tid_, sid_);
        break;
    case XS_XRESPONDENT:
        s = new (std::nothrow) xrespondent_t (parent_, tid_, sid_);
        break;
    default:
        errno = EINVAL;
        return NULL;
    }
    alloc_assert (s);
    int rc = s->init ();
    if (rc != 0)
        return NULL;
    return s;
}

xs::socket_base_t::socket_base_t (ctx_t *parent_, uint32_t tid_, int sid_) :
    own_t (parent_, tid_),
    tag (0xbaddecaf),
    ctx_terminated (false),
    destroyed (false),
    initialised (false),
    last_tsc (0),
    ticks (0),
    rcvmore (false)
{
    options.socket_id = sid_;
}

int xs::socket_base_t::init ()
{
    xs_assert (!initialised);
    int rc = mailbox_init (&mailbox);
    if (rc != 0) {
        destroyed = true;
        delete this;
        return -1;
    }
    initialised = true;
    return 0;
}

xs::socket_base_t::~socket_base_t ()
{
    xs_assert (destroyed);

    if (initialised)
        mailbox_close (&mailbox);
}

xs::mailbox_t *xs::socket_base_t::get_mailbox ()
{
    return &mailbox;
}

void xs::socket_base_t::stop ()
{
    //  Called by ctx when it is terminated (xs_term).
    //  'stop' command is sent from the threads that called xs_term to
    //  the thread owning the socket. This way, blocking call in the
    //  owner thread can be interrupted.
    send_stop ();
}

int xs::socket_base_t::parse_uri (const char *uri_,
    std::string &protocol_, std::string &address_)
{
    xs_assert (uri_ != NULL);

    std::string uri (uri_);
    std::string::size_type pos = uri.find ("://");
    if (pos == std::string::npos) {
        errno = EINVAL;
        return -1;
    }
    protocol_ = uri.substr (0, pos);
    address_ = uri.substr (pos + 3);
    if (protocol_.empty () || address_.empty ()) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int xs::socket_base_t::check_protocol (const std::string &protocol_)
{
    //  First check out whether the protcol is something we are aware of.
    if (protocol_ != "inproc" && protocol_ != "ipc" && protocol_ != "tcp" &&
          protocol_ != "pgm" && protocol_ != "epgm" && protocol_ != "udp") {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    //  If Crossroads is not compiled with OpenPGM, pgm and epgm transports
    //  are not avaialble.
#if !defined XS_HAVE_OPENPGM
    if (protocol_ == "pgm" || protocol_ == "epgm") {
        errno = EPROTONOSUPPORT;
        return -1;
    }
#endif

    //  IPC transport is not available on Windows and OpenVMS.
#if defined XS_HAVE_WINDOWS || defined XS_HAVE_OPENVMS
    if (protocol_ == "ipc") {
        //  Unknown protocol.
        errno = EPROTONOSUPPORT;
        return -1;
    }
#endif

    //  Check whether socket type and transport protocol match.
    //  Specifically, multicast protocols can't be combined with
    //  bi-directional messaging patterns (socket types).
    if ((protocol_ == "pgm" || protocol_ == "epgm" || protocol_ == "udp") &&
          options.type != XS_PUB && options.type != XS_SUB &&
          options.type != XS_XPUB && options.type != XS_XSUB) {
        errno = ENOCOMPATPROTO;
        return -1;
    }

    //  Protocol is available.
    return 0;
}

void xs::socket_base_t::attach_pipe (pipe_t *pipe_, bool icanhasall_)
{
    //  First, register the pipe so that we can terminate it later on.
    pipe_->set_event_sink (this);
    pipes.push_back (pipe_);
    
    //  Let the derived socket type know about new pipe.
    xattach_pipe (pipe_, icanhasall_);

    //  If the socket is already being closed, ask any new pipes to terminate
    //  straight away.
    if (is_terminating ()) {
        register_term_acks (1);
        pipe_->terminate (false);
    }
}

int xs::socket_base_t::setsockopt (int option_, const void *optval_,
    size_t optvallen_)
{
    if (unlikely (ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  First, check whether specific socket type overloads the option.
    int rc = xsetsockopt (option_, optval_, optvallen_);
    if (rc == 0 || errno != EINVAL)
        return rc;

    //  If the socket type doesn't support the option, pass it to
    //  the generic option parser.
    rc = options.setsockopt (option_, optval_, optvallen_);
    return rc;
}

int xs::socket_base_t::getsockopt (int option_, void *optval_,
    size_t *optvallen_)
{
    if (unlikely (ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    if (option_ == XS_RCVMORE) {
        if (*optvallen_ < sizeof (int)) {
            errno = EINVAL;
            return -1;
        }
        *((int*) optval_) = rcvmore ? 1 : 0;
        *optvallen_ = sizeof (int);
        return 0;
    }

    if (option_ == XS_FD) {
        if (*optvallen_ < sizeof (fd_t)) {
            errno = EINVAL;
            return -1;
        }
        *((fd_t*) optval_) = mailbox_fd (&mailbox);
        *optvallen_ = sizeof (fd_t);
        return 0;
    }

    if (option_ == XS_EVENTS) {
        if (*optvallen_ < sizeof (int)) {
            errno = EINVAL;
            return -1;
        }
        int rc = process_commands (0, false);
        if (rc != 0 && (errno == EINTR || errno == ETERM))
            return -1;
        errno_assert (rc == 0);
        *((int*) optval_) = 0;
        if (has_out ())
            *((int*) optval_) |= XS_POLLOUT;
        if (has_in ())
            *((int*) optval_) |= XS_POLLIN;
        *optvallen_ = sizeof (int);
        return 0;
    }

    return options.getsockopt (option_, optval_, optvallen_);
}

int xs::socket_base_t::bind (const char *addr_)
{
    if (unlikely (ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Parse addr_ string.
    std::string protocol;
    std::string address;
    int rc = parse_uri (addr_, protocol, address);
    if (rc != 0)
        return -1;

    rc = check_protocol (protocol);
    if (rc != 0)
        return -1;

    if (protocol == "inproc") {
        endpoint_t endpoint = {this, options};
        rc = register_endpoint (addr_, endpoint);
        if (rc != 0)
            return -1;

        //  Endpoint IDs for inproc transport are not implemented at the
        //  moment. Thus we return 0 to the user.
        return 0;
    }

    if (protocol == "pgm" || protocol == "epgm" || protocol == "udp") {

        //  For convenience's sake, bind can be used interchageable with
        //  connect for PGM and EPGM transports.
        rc = connect (addr_);
        return rc;
    }

    //  Remaining trasnports require to be run in an I/O thread, so at this
    //  point we'll choose one.
    io_thread_t *thread = choose_io_thread (options.affinity);
    xs_assert (thread);

    if (protocol == "tcp") {
        tcp_listener_t *listener = new (std::nothrow) tcp_listener_t (
            thread, this, options);
        alloc_assert (listener);
        rc = listener->set_address (address.c_str ());
        if (rc != 0) {
            delete listener;
            return -1;
        }
        launch_child (listener);
        return add_endpoint (listener);
    }

#if !defined XS_HAVE_WINDOWS && !defined XS_HAVE_OPENVMS
    if (protocol == "ipc") {
        ipc_listener_t *listener = new (std::nothrow) ipc_listener_t (
            thread, this, options);
        alloc_assert (listener);
        rc = listener->set_address (address.c_str ());
        if (rc != 0) {
            delete listener;
            return -1;
        }
        launch_child (listener);
        return add_endpoint (listener);
    }
#endif

    xs_assert (false);
    return -1;
}

int xs::socket_base_t::connect (const char *addr_)
{
    if (unlikely (ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Parse addr_ string.
    std::string protocol;
    std::string address;
    int rc = parse_uri (addr_, protocol, address);
    if (rc != 0)
        return -1;

    rc = check_protocol (protocol);
    if (rc != 0)
        return -1;

    if (protocol == "inproc") {

        //  TODO: inproc connect is specific with respect to creating pipes
        //  as there's no 'reconnect' functionality implemented. Once that
        //  is in place we should follow generic pipe creation algorithm.

        //  Find the peer endpoint.
        endpoint_t peer = find_endpoint (addr_);
        if (!peer.socket)
            return -1;

        // The total HWM for an inproc connection should be the sum of
        // the binder's HWM and the connector's HWM.
        int  sndhwm;
        int  rcvhwm;
        if (options.sndhwm == 0 || peer.options.rcvhwm == 0)
            sndhwm = 0;
        else
            sndhwm = options.sndhwm + peer.options.rcvhwm;
        if (options.rcvhwm == 0 || peer.options.sndhwm == 0)
            rcvhwm = 0;
        else
            rcvhwm = options.rcvhwm + peer.options.sndhwm;

        //  Create a bi-directional pipe to connect the peers.
        object_t *parents [2] = {this, peer.socket};
        pipe_t *ppair [2] = {NULL, NULL};
        int hwms [2] = {sndhwm, rcvhwm};
        bool delays [2] = {options.delay_on_disconnect, options.delay_on_close};
        rc = pipepair (parents, ppair, hwms, delays, options.sp_version);
        errno_assert (rc == 0);

        //  Attach local end of the pipe to this socket object.
        attach_pipe (ppair [0]);

        //  If required, send the identity of the local socket to the peer.
        if (options.send_identity) {
            msg_t id;
            rc = id.init_size (options.identity_size);
            errno_assert (rc == 0);
            memcpy (id.data (), options.identity, options.identity_size);
            id.set_flags (msg_t::identity);
            bool written = ppair [0]->write (&id);
            xs_assert (written);
            pipes [0]->flush ();
        }

        //  If required, send the identity of the peer to the local socket.
        if (peer.options.send_identity) {
            msg_t id;
            rc = id.init_size (peer.options.identity_size);
            errno_assert (rc == 0);
            memcpy (id.data (), peer.options.identity,
                peer.options.identity_size);
            id.set_flags (msg_t::identity);
            bool written = ppair [1]->write (&id);
            xs_assert (written);
            ppair [1]->flush ();
        }

        //  Attach remote end of the pipe to the peer socket. Note that peer's
        //  seqnum was incremented in find_endpoint function. We don't need it
        //  increased here.
        send_bind (peer.socket, ppair [1], false);

        //  Inproc endpoints are not yet implemented thus we return 0.
        return 0;
    }

    //  Choose the I/O thread to run the session in.
    io_thread_t *thread = choose_io_thread (options.affinity);
    xs_assert (thread);

    if (protocol == "tcp") {
        tcp_connecter_t connecter (thread, NULL, options, false);
        int rc = connecter.set_address (address.c_str());
        if (rc != 0) {
            return -1;
        }
    }

#if !defined XS_HAVE_WINDOWS && !defined XS_HAVE_OPENVMS
    if (protocol == "ipc") {
        ipc_connecter_t connecter (thread, NULL, options, false);
        int rc = connecter.set_address (address.c_str());
        if (rc != 0) {
            return -1;
        }
    }
#endif

#ifdef XS_HAVE_OPENPGM
    if (protocol == "pgm" || protocol == "epgm") {
        struct pgm_addrinfo_t *res = NULL;
        uint16_t port_number = 0;
        int rc = pgm_socket_t::init_address(address.c_str(), &res, &port_number);
        if (res != NULL)
            pgm_freeaddrinfo (res);
        if (rc != 0 || port_number == 0)
            return -1;
    }
#endif

    //  Create session.
    session_base_t *session = session_base_t::create (thread, true, this,
        options, protocol.c_str (), address.c_str ());
    errno_assert (session);

    //  Create a bi-directional pipe.
    object_t *parents [2] = {this, session};
    pipe_t *ppair [2] = {NULL, NULL};
    int hwms [2] = {options.sndhwm, options.rcvhwm};
    bool delays [2] = {options.delay_on_disconnect, options.delay_on_close};
    rc = pipepair (parents, ppair, hwms, delays, options.sp_version);
    errno_assert (rc == 0);

    // PGM does not support subscription forwarding; ask for all data to be
    // sent to this pipe.
    bool icanhasall = false;
    if (protocol == "pgm" || protocol == "epgm")
        icanhasall = true;

    //  Attach local end of the pipe to the socket object.
    attach_pipe (ppair [0], icanhasall);

    //  Attach remote end of the pipe to the session object later on.
    session->attach_pipe (ppair [1]);

    //  Activate the session. Make it a child of this socket.
    launch_child (session);
    return add_endpoint (session);
}

int xs::socket_base_t::shutdown (int how_)
{
    //  Check whether the library haven't been shut down yet.
    if (unlikely (ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Endpoint ID means 'shutdown not implemented'.
    if (how_ <= 0) {
        errno = ENOTSUP;
        return -1;
    }

    //  Find the endpoint corresponding to the ID.
    endpoints_t::iterator it = endpoints.find (how_);
    if (it == endpoints.end ()) {
        errno = EINVAL;
        return -1;
    }

    process_term_req (it->second);
    endpoints.erase (it);
    return 0;
}

int xs::socket_base_t::send (msg_t *msg_, int flags_)
{
    //  Check whether the library haven't been shut down yet.
    if (unlikely (ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Check whether message passed to the function is valid.
    if (unlikely (!msg_ || !msg_->check ())) {
        errno = EFAULT;
        return -1;
    }

    //  Process pending commands, if any.
    int rc = process_commands (0, true);
    if (unlikely (rc != 0))
        return -1;

    //  Clear any user-visible flags that are set on the message.
    msg_->reset_flags (msg_t::more);

    //  At this point we impose the flags on the message.
    if (flags_ & XS_SNDMORE)
        msg_->set_flags (msg_t::more);

    //  Try to send the message.
    rc = xsend (msg_, flags_);
    if (rc == 0)
        return 0;
    if (unlikely (errno != EAGAIN))
        return -1;

    int timeout = sndtimeo ();
    if (flags_ & XS_DONTWAIT || timeout == 0) {

        //  It looks like pipe is full. However, previous process_commands may
        //  have done nothing because of the throttling. Thus, let's give it
        //  last try and force commands to be processed. Then try to re-send
        //  the message.
        rc = process_commands (0, false);
        if (unlikely (rc != 0))
            return -1;
        return xsend (msg_, flags_);
    }

    //  Compute the time when the timeout should occur.
    //  If the timeout is infite, don't care. 
    uint64_t end = timeout < 0 ? 0 : (clock.now_ms () + timeout);

    //  Oops, we couldn't send the message. Wait for the next
    //  command, process it and try to send the message again.
    //  If timeout is reached in the meantime, return EAGAIN.
    while (true) {
        if (unlikely (process_commands (timeout, false) != 0))
            return -1;
        rc = xsend (msg_, flags_);
        if (rc == 0)
            break;
        if (unlikely (errno != EAGAIN))
            return -1;
        if (timeout > 0) {
            timeout = (int) (end - clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                return -1;
            }
        }
    }

    return 0;
}

int xs::socket_base_t::recv (msg_t *msg_, int flags_)
{
    //  Check whether the library haven't been shut down yet.
    if (unlikely (ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Check whether message passed to the function is valid.
    if (unlikely (!msg_ || !msg_->check ())) {
        errno = EFAULT;
        return -1;
    }

    //  Get the message.
    int rc = xrecv (msg_, flags_);
    if (unlikely (rc != 0 && errno != EAGAIN))
        return -1;

    //  Once every inbound_poll_rate messages check for signals and process
    //  incoming commands. This happens only if we are not polling altogether
    //  because there are messages available all the time. If poll occurs,
    //  ticks is set to zero and thus we avoid this code.
    //
    //  Note that 'recv' uses different command throttling algorithm (the one
    //  described above) from the one used by 'send'. This is because counting
    //  ticks is more efficient than doing RDTSC all the time.
    if (++ticks == inbound_poll_rate) {
        if (unlikely (process_commands (0, false) != 0))
            return -1;
        ticks = 0;
    }

    //  If we have the message, return immediately.
    if (rc == 0) {
        extract_flags (msg_);
        return 0;
    }

    //  If the message cannot be fetched immediately, there are two scenarios.
    //  For non-blocking recv, commands are processed in case there's an
    //  activate_reader command already waiting int a command pipe.
    //  If it's not, return EAGAIN.
    int timeout = rcvtimeo ();
    if (flags_ & XS_DONTWAIT || timeout == 0) {
        if (unlikely (process_commands (0, false) != 0))
            return -1;
        ticks = 0;

        rc = xrecv (msg_, flags_);
        if (rc < 0)
            return rc;

        extract_flags (msg_);
        return 0;
    }

    //  Compute the time when the timeout should occur.
    //  If the timeout is infite, don't care. 
    uint64_t end = timeout < 0 ? 0 : (clock.now_ms () + timeout);

    //  In blocking scenario, commands are processed over and over again until
    //  we are able to fetch a message.
    bool block = (ticks != 0);
    while (true) {
        if (unlikely (process_commands (block ? timeout : 0, false) != 0))
            return -1;
        rc = xrecv (msg_, flags_);
        if (rc == 0) {
            ticks = 0;
            break;
        }
        if (unlikely (errno != EAGAIN))
            return -1;
        block = true;
        if (timeout > 0) {
            timeout = (int) (end - clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                return -1;
            }
        }
    }

    extract_flags (msg_);
    return 0;
}

int xs::socket_base_t::close ()
{
    //  Mark the socket as dead.
    tag = 0xdeadbeef;

    //  Transfer the ownership of the socket from this application thread
    //  to the reaper thread which will take care of the rest of shutdown
    //  process.
    send_reap (this);

    return 0;
}

bool xs::socket_base_t::has_in ()
{
    bool ret = xhas_in ();
    return ret;
}

bool xs::socket_base_t::has_out ()
{
    bool ret = xhas_out ();
    return ret;
}

void xs::socket_base_t::start_reaping (io_thread_t *io_thread_)
{
    //  Plug the socket to the reaper thread.
    io_thread = io_thread_;
    handle = io_thread->add_fd (mailbox_fd (&mailbox), this);
    io_thread->set_pollin (handle);

    //  Initialise the termination and check whether it can be deallocated
    //  immediately.
    terminate ();
    check_destroy ();
}

int xs::socket_base_t::process_commands (int timeout_, bool throttle_)
{
    int rc;
    command_t cmd;
    if (timeout_ != 0) {

        //  If we are asked to wait, simply ask mailbox to wait.
        rc = mailbox_recv (&mailbox, &cmd, timeout_);
    }
    else {

        //  If we are asked not to wait, check whether we haven't processed
        //  commands recently, so that we can throttle the new commands.
        //  This doesn't apply when the throttling is turned off.
        if (throttle_) {

            //  Get the CPU's tick counter. If 0, the counter is not available.
            uint64_t tsc = xs::clock_t::rdtsc ();

            //  Optimised version of command processing - it doesn't have to
            //  check for incoming commands each time. It does so only if
            //  certain time elapsed since last command processing. Command
            //  delay varies depending on CPU speed: With max_command_delay set
            //  to 3000000 it's ~1ms on 3GHz CPU, ~2ms on 1.5GHz CPU etc.
            //  The optimisation makes sense only on platforms where getting 
            //  timestamp is a very cheap operation (tens of nanoseconds).
            if (tsc) {

                //  Check whether TSC haven't jumped backwards (in case of
                //  migration between CPU cores) and whether certain time have
                //  elapsed since last command processing. If it didn't do
                //  nothing.
                if (tsc >= last_tsc && tsc - last_tsc <= max_command_delay)
                    return 0;
                last_tsc = tsc;
            }
        }

        //  Check whether there are any commands pending for this thread.
        rc = mailbox_recv (&mailbox, &cmd, 0);
    }

    //  Process all the commands available at the moment.
    while (true) {
        if (rc == -1 && errno == EAGAIN)
            break;
        if (rc == -1 && errno == EINTR)
            return -1;
        errno_assert (rc == 0);
        cmd.destination->process_command (cmd);
        rc = mailbox_recv (&mailbox, &cmd, 0);
     }

    if (ctx_terminated) {
        errno = ETERM;
        return -1;
    }

    return 0;
}

void xs::socket_base_t::process_stop ()
{
    //  Here, someone have called xs_term while the socket was still alive.
    //  We'll remember the fact so that any blocking call is interrupted and any
    //  further attempt to use the socket will return ETERM. The user is still
    //  responsible for calling xs_close on the socket though!
    ctx_terminated = true;
}

void xs::socket_base_t::process_bind (pipe_t *pipe_)
{
    attach_pipe (pipe_);
}

void xs::socket_base_t::process_unplug ()
{
}

void xs::socket_base_t::process_term (int linger_)
{
    //  Unregister all inproc endpoints associated with this socket.
    //  Doing this we make sure that no new pipes from other sockets (inproc)
    //  will be initiated.
    unregister_endpoints (this);

    //  Ask all attached pipes to terminate.
    for (pipes_t::size_type i = 0; i != pipes.size (); ++i)
        pipes [i]->terminate (false);
    register_term_acks ((int) pipes.size ());

    //  Continue the termination process immediately.
    own_t::process_term (linger_);
}

void xs::socket_base_t::process_destroy ()
{
    destroyed = true;
}

int xs::socket_base_t::xsetsockopt (int option_, const void *optval_,
    size_t optvallen_)
{
    errno = EINVAL;
    return -1;
}

bool xs::socket_base_t::xhas_out ()
{
    return false;
}

int xs::socket_base_t::xsend (msg_t *msg_, int flags_)
{
    errno = ENOTSUP;
    return -1;
}

bool xs::socket_base_t::xhas_in ()
{
    return false;
}

int xs::socket_base_t::xrecv (msg_t *msg_, int flags_)
{
    errno = ENOTSUP;
    return -1;
}

void xs::socket_base_t::xread_activated (pipe_t *pipe_)
{
    xs_assert (false);
}
void xs::socket_base_t::xwrite_activated (pipe_t *pipe_)
{
    xs_assert (false);
}

void xs::socket_base_t::xhiccuped (pipe_t *pipe_)
{
    xs_assert (false);
}

void xs::socket_base_t::in_event (fd_t fd_)
{
    //  This function is invoked only once the socket is running in the context
    //  of the reaper thread. Process any commands from other threads/sockets
    //  that may be available at the moment. Ultimately, the socket will
    //  be destroyed.
    process_commands (0, false);
    check_destroy ();
}

void xs::socket_base_t::out_event (fd_t fd_)
{
    xs_assert (false);
}

void xs::socket_base_t::timer_event (handle_t handle_)
{
    xs_assert (false);
}

void xs::socket_base_t::check_destroy ()
{
    //  If the object was already marked as destroyed, finish the deallocation.
    if (destroyed) {

        //  Remove the socket from the reaper's I/O thread.
        io_thread->rm_fd (handle);

        //  Remove the socket from the context.
        destroy_socket (this);

        //  Notify the reaper about the fact.
        send_reaped ();

        //  Deallocate.
        own_t::process_destroy ();
    }
}

void xs::socket_base_t::read_activated (pipe_t *pipe_)
{
    xread_activated (pipe_);
}

void xs::socket_base_t::write_activated (pipe_t *pipe_)
{
    xwrite_activated (pipe_);
}

void xs::socket_base_t::hiccuped (pipe_t *pipe_)
{
    xhiccuped (pipe_);
}

void xs::socket_base_t::terminated (pipe_t *pipe_)
{
    //  Notify the specific socket type about the pipe termination.
    xterminated (pipe_);

    //  Remove the pipe from the list of attached pipes and confirm its
    //  termination if we are already shutting down.
    pipes.erase (pipe_);
    if (is_terminating ())
        unregister_term_ack ();
}

void xs::socket_base_t::extract_flags (msg_t *msg_)
{
    //  Test whether IDENTITY flag is valid for this socket type.
    //  TODO: Connection should be closed here!
    if (unlikely (msg_->flags () & msg_t::identity))
        xs_assert (options.recv_identity);
  
    //  Remove MORE flag.
    rcvmore = msg_->flags () & msg_t::more ? true : false;
}

int xs::socket_base_t::rcvtimeo ()
{
    return options.rcvtimeo;
}

int xs::socket_base_t::sndtimeo ()
{
    return options.sndtimeo;
}

uint64_t xs::socket_base_t::now_ms ()
{
    return clock.now_ms ();
}

int xs::socket_base_t::add_endpoint (own_t *endpoint_)
{
    //  Get a unique endpoint ID.
    int id = 1;
    for (endpoints_t::iterator it = endpoints.begin (); it != endpoints.end ();
          ++it, ++id)
        if (it->first != id)
            break;

    //  Remember the endpoint.
    endpoints.insert (std::make_pair (id, endpoint_));
    return id;
}

