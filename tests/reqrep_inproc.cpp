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

#include "testutil.hpp"

int XS_TEST_MAIN ()
{
    fprintf (stderr, "reqrep_inproc test running...\n");

    void *ctx = xs_init ();
    errno_assert (ctx);

    void *sb = xs_socket (ctx, XS_REP);
    errno_assert (sb);
    int rc = xs_bind (sb, "inproc://a");
    errno_assert (rc != -1);

    void *sc = xs_socket (ctx, XS_REQ);
    errno_assert (sc);
    rc = xs_connect (sc, "inproc://a");
    errno_assert (rc != -1);
    
    bounce (sb, sc);

    rc = xs_close (sc);
    errno_assert (rc == 0);

    rc = xs_close (sb);
    errno_assert (rc == 0);

    rc = xs_term (ctx);
    errno_assert (rc == 0);

    return 0 ;
}
