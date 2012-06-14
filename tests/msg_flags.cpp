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

#include "testutil.hpp"

int XS_TEST_MAIN ()
{
    fprintf (stderr, "msg_flags test running...\n");

    //  Create the infrastructure
    void *ctx = xs_init ();
    errno_assert (ctx);
    void *sb = xs_socket (ctx, XS_XREP);
    errno_assert (sb);
    int rc = xs_bind (sb, "inproc://a");
    errno_assert (rc != -1);
    void *sc = xs_socket (ctx, XS_XREQ);
    errno_assert (sc);
    rc = xs_connect (sc, "inproc://a");
    errno_assert (rc != -1);
   
    //  Send 2-part message.
    rc = xs_send (sc, "A", 1, XS_SNDMORE);
    errno_assert (rc == 1);
    rc = xs_send (sc, "B", 1, 0);
    errno_assert (rc == 1);

    //  Identity comes first.
    xs_msg_t msg;
    rc = xs_msg_init (&msg);
    errno_assert (rc == 0);
    rc = xs_recvmsg (sb, &msg, 0);
    errno_assert (rc >= 0);
    int more;
    size_t more_size = sizeof (more);
    rc = xs_getmsgopt (&msg, XS_MORE, &more, &more_size);
    errno_assert (rc == 0);
    assert (more == 1);

    //  Then the first part of the message body.
    rc = xs_recvmsg (sb, &msg, 0);
    errno_assert (rc == 1);
    more_size = sizeof (more);
    rc = xs_getmsgopt (&msg, XS_MORE, &more, &more_size);
    errno_assert (rc == 0);
    assert (more == 1);

    //  And finally, the second part of the message body.
    rc = xs_recvmsg (sb, &msg, 0);
    errno_assert (rc == 1);
    more_size = sizeof (more);
    rc = xs_getmsgopt (&msg, XS_MORE, &more, &more_size);
    errno_assert (rc == 0);
    assert (more == 0);

    //  Deallocate the infrastructure.
    rc = xs_close (sc);
    errno_assert (rc == 0);
    rc = xs_close (sb);
    errno_assert (rc == 0);
    rc = xs_term (ctx);
    errno_assert (rc == 0);
    return 0 ;
}

