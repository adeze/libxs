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

#ifndef __XSZMQ_UTILS_H_INCLUDED__
#define __XSZMQ_UTILS_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
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

/*  Helper functions are used by perf tests so that they don't have to care   */
/*  about minutiae of time-related functions on different OS platforms.       */

/*  Starts the stopwatch. Returns the handle to the watch.                    */
XSZMQ_EXPORT void *zmq_stopwatch_start (void);

/*  Stops the stopwatch. Returns the number of microseconds elapsed since     */
/*  the stopwatch was started.                                                */
XSZMQ_EXPORT unsigned long zmq_stopwatch_stop (void *watch_);

/*  Sleeps for specified number of seconds.                                   */
XSZMQ_EXPORT void zmq_sleep (int seconds_);

#undef XSZMQ_EXPORT

#ifdef __cplusplus
}
#endif

#endif
