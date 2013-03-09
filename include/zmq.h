/*
    Copyright (c) 2012 Martin Lucina
    Copyright (c) 2007-2011 iMatix Corporation
    Copyright (c) 2007-2011 Other contributors as noted in the AUTHORS file

    This file is part of Crossroads I/O.

    Crossroads I/O is free software; you can redistribute it and/or modify it
    under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Crossroads I/O is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __XSZMQ_H_INCLUDED__
#define __XSZMQ_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <stddef.h>
#if defined _WIN32
#include <winsock2.h>
#endif

/*  Handle DSO symbol visibility                                             */
#if defined _WIN32
// XS_STATIC. Suggested here: http://stackoverflow.com/questions/6259022/how-can-i-handle-dll-export-when-compiling-dll-to-a-static-library
// toggle the XS_STATIC or DLL_EXPORT macros in builds/msvc/properties/Crossroads.props
#   if defined XS_STATIC
#       define XSZMQ_EXPORT
#   else
#       if defined DLL_EXPORT
#           define XSZMQ_EXPORT __declspec(dllexport)
#       else
#           define XSZMQ_EXPORT __declspec(dllimport)
#       endif
#   endif
#else
#   if defined __SUNPRO_C  || defined __SUNPRO_CC
#       define XSZMQ_EXPORT __global
#   elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#       define XSZMQ_EXPORT __attribute__ ((visibility("default")))
#   else
#       define XSZMQ_EXPORT
#   endif
#endif

/******************************************************************************/
/*  0MQ versioning support.                                                   */
/******************************************************************************/

/*  Version macros for compile-time API version detection                     */
#define ZMQ_VERSION_MAJOR 2
#define ZMQ_VERSION_MINOR 1
#define ZMQ_VERSION_PATCH 11

#define ZMQ_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define ZMQ_VERSION \
    ZMQ_MAKE_VERSION(ZMQ_VERSION_MAJOR, ZMQ_VERSION_MINOR, ZMQ_VERSION_PATCH)

/*  Run-time API version detection                                            */
XSZMQ_EXPORT void zmq_version (int *major, int *minor, int *patch);

/******************************************************************************/
/*  0MQ errors.                                                               */
/******************************************************************************/

/*  These constants conflict with xs.h. Disable them if we are building the   */
/*  libxszmq compatibility library itself.                                    */
#ifndef XS_BUILDING_LIBXSZMQ

/*  A number random enough not to collide with different errno ranges on      */
/*  different OSes. The assumption is that error_t is at least 32-bit type.   */
#define ZMQ_HAUSNUMERO 156384712

/*  On Windows platform some of the standard POSIX errnos are not defined.    */
#ifndef ENOTSUP
#define ENOTSUP (ZMQ_HAUSNUMERO + 1)
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (ZMQ_HAUSNUMERO + 2)
#endif
#ifndef ENOBUFS
#define ENOBUFS (ZMQ_HAUSNUMERO + 3)
#endif
#ifndef ENETDOWN
#define ENETDOWN (ZMQ_HAUSNUMERO + 4)
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (ZMQ_HAUSNUMERO + 5)
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (ZMQ_HAUSNUMERO + 6)
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (ZMQ_HAUSNUMERO + 7)
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (ZMQ_HAUSNUMERO + 8)
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (ZMQ_HAUSNUMERO + 9)
#endif

/*  Native 0MQ error codes.                                                   */
#ifndef EFSM
#define EFSM (ZMQ_HAUSNUMERO + 51)
#endif
#ifndef ENOCOMPATPROTO
#define ENOCOMPATPROTO (ZMQ_HAUSNUMERO + 52)
#endif
#ifndef ETERM
#define ETERM (ZMQ_HAUSNUMERO + 53)
#endif
#ifndef EMTHREAD
#define EMTHREAD (ZMQ_HAUSNUMERO + 54)
#endif

#endif /*  XS_BUILDING_LIBXSZMQ                                               */

/*  This function retrieves the errno as it is known to 0MQ library. The goal */
/*  of this function is to make the code 100% portable, including where 0MQ   */
/*  compiled with certain CRT library (on Windows) is linked to an            */
/*  application that uses different CRT library.                              */
XSZMQ_EXPORT int zmq_errno (void);

/*  Resolves system errors and 0MQ errors to human-readable string.           */
XSZMQ_EXPORT const char *zmq_strerror (int errnum);

/******************************************************************************/
/*  0MQ message definition.                                                   */
/******************************************************************************/

/*  Maximal size of "Very Small Message". VSMs are passed by value            */
/*  to avoid excessive memory allocation/deallocation.                        */
/*  If VMSs larger than 255 bytes are required, type of 'vsm_size'            */
/*  field in zmq_msg_t structure should be modified accordingly.              */
#define ZMQ_MAX_VSM_SIZE 30

/*  Message types. These integers may be stored in 'content' member of the    */
/*  message instead of regular pointer to the data.                           */
#define ZMQ_DELIMITER 31
#define ZMQ_VSM 32

/*  Message flags. ZMQ_MSG_SHARED is strictly speaking not a message flag     */
/*  (it has no equivalent in the wire format), however, making  it a flag     */
/*  allows us to pack the stucture tigher and thus improve performance.       */
#define ZMQ_MSG_MORE 1
#define ZMQ_MSG_SHARED 128
#define ZMQ_MSG_MASK 129 /* Merges all the flags */

/*  A message. Note that 'content' is not a pointer to the raw data.          */
/*  Rather it is pointer to zmq::msg_content_t structure                      */
/*  (see src/msg_content.hpp for its definition).                             */
typedef struct
{
    void *content;
    unsigned char flags;
    unsigned char vsm_size;
    unsigned char vsm_data [ZMQ_MAX_VSM_SIZE];
} zmq_msg_t;

typedef void (zmq_free_fn) (void *data, void *hint);

XSZMQ_EXPORT int zmq_msg_init (zmq_msg_t *msg);
XSZMQ_EXPORT int zmq_msg_init_size (zmq_msg_t *msg, size_t size);
XSZMQ_EXPORT int zmq_msg_init_data (zmq_msg_t *msg, void *data,
    size_t size, zmq_free_fn *ffn, void *hint);
XSZMQ_EXPORT int zmq_msg_close (zmq_msg_t *msg);
XSZMQ_EXPORT int zmq_msg_move (zmq_msg_t *dest, zmq_msg_t *src);
XSZMQ_EXPORT int zmq_msg_copy (zmq_msg_t *dest, zmq_msg_t *src);
XSZMQ_EXPORT void *zmq_msg_data (zmq_msg_t *msg);
XSZMQ_EXPORT size_t zmq_msg_size (zmq_msg_t *msg);

/******************************************************************************/
/*  0MQ infrastructure (a.k.a. context) initialisation & termination.         */
/******************************************************************************/

XSZMQ_EXPORT void *zmq_init (int io_threads);
XSZMQ_EXPORT int zmq_term (void *context);

/******************************************************************************/
/*  0MQ socket definition.                                                    */
/******************************************************************************/

/*  Socket types.                                                             */ 
#define ZMQ_PAIR 0
#define ZMQ_PUB 1
#define ZMQ_SUB 2
#define ZMQ_REQ 3
#define ZMQ_REP 4
#define ZMQ_DEALER 5
#define ZMQ_ROUTER 6
#define ZMQ_PULL 7
#define ZMQ_PUSH 8
#define ZMQ_XPUB 9
#define ZMQ_XSUB 10
#define ZMQ_XREQ ZMQ_DEALER        /*  Old alias, remove in 3.x               */
#define ZMQ_XREP ZMQ_ROUTER        /*  Old alias, remove in 3.x               */
#define ZMQ_UPSTREAM ZMQ_PULL      /*  Old alias, remove in 3.x               */
#define ZMQ_DOWNSTREAM ZMQ_PUSH    /*  Old alias, remove in 3.x               */

/*  Socket options.                                                           */
#define ZMQ_HWM 1
#define ZMQ_SWAP 3
#define ZMQ_AFFINITY 4
#define ZMQ_IDENTITY 5
#define ZMQ_SUBSCRIBE 6
#define ZMQ_UNSUBSCRIBE 7
#define ZMQ_RATE 8
#define ZMQ_RECOVERY_IVL 9
#define ZMQ_MCAST_LOOP 10
#define ZMQ_SNDBUF 11
#define ZMQ_RCVBUF 12
#define ZMQ_RCVMORE 13
#define ZMQ_FD 14
#define ZMQ_EVENTS 15
#define ZMQ_TYPE 16
#define ZMQ_LINGER 17
#define ZMQ_RECONNECT_IVL 18
#define ZMQ_BACKLOG 19
#define ZMQ_RECOVERY_IVL_MSEC 20   /*  opt. recovery time, reconcile in 3.x   */
#define ZMQ_RECONNECT_IVL_MAX 21
    
/*  Send/recv options.                                                        */
#define ZMQ_NOBLOCK 1
#define ZMQ_SNDMORE 2

XSZMQ_EXPORT void *zmq_socket (void *context, int type);
XSZMQ_EXPORT int zmq_close (void *s);
XSZMQ_EXPORT int zmq_setsockopt (void *s, int option, const void *optval,
    size_t optvallen); 
XSZMQ_EXPORT int zmq_getsockopt (void *s, int option, void *optval,
    size_t *optvallen);
XSZMQ_EXPORT int zmq_bind (void *s, const char *addr);
XSZMQ_EXPORT int zmq_connect (void *s, const char *addr);
XSZMQ_EXPORT int zmq_send (void *s, zmq_msg_t *msg, int flags);
XSZMQ_EXPORT int zmq_recv (void *s, zmq_msg_t *msg, int flags);

/******************************************************************************/
/*  I/O multiplexing.                                                         */
/******************************************************************************/

#define ZMQ_POLLIN 1
#define ZMQ_POLLOUT 2
#define ZMQ_POLLERR 4

typedef struct
{
    void *socket;
#if defined _WIN32
    SOCKET fd;
#else
    int fd;
#endif
    short events;
    short revents;
} zmq_pollitem_t;

XSZMQ_EXPORT int zmq_poll (zmq_pollitem_t *items, int nitems, long timeout);

/******************************************************************************/
/*  Built-in devices                                                          */
/******************************************************************************/

#define ZMQ_STREAMER 1
#define ZMQ_FORWARDER 2
#define ZMQ_QUEUE 3

XSZMQ_EXPORT int zmq_device (int device, void * insocket, void* outsocket);

#undef XSZMQ_EXPORT

#ifdef __cplusplus
}
#endif

#endif
