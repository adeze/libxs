/*
    Copyright (c) 2011-2012 250bpm s.r.o.
    Copyright (c) 2011 VMware, Inc.
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

#include "dist.hpp"
#include "pipe.hpp"
#include "err.hpp"
#include "msg.hpp"
#include "likely.hpp"

xs::dist_t::dist_t () :
    matching (0),
    active (0),
    eligible (0),
    more (false)
{
}

xs::dist_t::~dist_t ()
{
    xs_assert (pipes.empty ());
}

void xs::dist_t::attach (pipe_t *pipe_)
{
    //  If we are in the middle of sending a message, we'll add new pipe
    //  into the list of eligible pipes. Otherwise we add it to the list
    //  of active pipes.
    if (more) {
        pipes.push_back (pipe_);
        pipes.swap (eligible, pipes.size () - 1);
        eligible++;
    }
    else {
        pipes.push_back (pipe_);
        pipes.swap (active, pipes.size () - 1);
        active++;
        eligible++;
    }
}

void xs::dist_t::match (pipe_t *pipe_)
{
    //  If pipe is already matching do nothing.
    if (pipes.index (pipe_) < matching)
        return;

    //  If the pipe isn't eligible, ignore it.
    if (pipes.index (pipe_) >= eligible)
        return;

    //  Mark the pipe as matching.
    pipes.swap (pipes.index (pipe_), matching);
    matching++;    
}

void xs::dist_t::unmatch ()
{
    matching = 0;
}

void xs::dist_t::terminated (pipe_t *pipe_)
{
    //  Remove the pipe from the list; adjust number of matching, active and/or
    //  eligible pipes accordingly.

    int i = pipes.index (pipe_);
    if (i < (int) matching) {
        pipes [i] = pipes [matching - 1];
        pipes [matching - 1] = NULL;
        if (pipes [i])
            ((pipes_t::item_t*) pipes [i])->set_array_index (i);
        --matching;
        i = matching;
    }
    if (i < (int) active) {
        pipes [i] = pipes [active - 1];
        pipes [active - 1] = NULL;
        if (pipes [i])
            ((pipes_t::item_t*) pipes [i])->set_array_index (i);
        --active;
        i = active;
    }
    if (i < (int) eligible) {
        pipes [i] = pipes [eligible - 1];
        pipes [eligible - 1] = NULL;
        if (pipes [i])
            ((pipes_t::item_t*) pipes [i])->set_array_index (i);
        --eligible;
        i = eligible;
    }
    pipes.erase (i);
}

void xs::dist_t::activated (pipe_t *pipe_)
{
    //  Move the pipe from passive to eligible state.
    pipes.swap (pipes.index (pipe_), eligible);
    eligible++;

    //  If there's no message being sent at the moment, move it to
    //  the active state.
    if (!more) {
        pipes.swap (eligible - 1, active);
        active++;
    }
}

int xs::dist_t::send_to_all (msg_t *msg_, int flags_)
{
    matching = active;
    return send_to_matching (msg_, flags_);
}

int xs::dist_t::send_to_matching (msg_t *msg_, int flags_)
{
    //  Is this end of a multipart message?
    bool msg_more = msg_->flags () & msg_t::more ? true : false;

    //  Push the message to matching pipes.
    distribute (msg_, flags_);

    //  If mutlipart message is fully sent, activate all the eligible pipes.
    if (!msg_more)
        active = eligible;

    more = msg_more;

    return 0;
}

void xs::dist_t::distribute (msg_t *msg_, int flags_)
{
    //  If there are no matching pipes available, simply drop the message.
    if (matching == 0) {
        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return;
    }

    if (msg_->is_vsm ()) {
        for (pipes_t::size_type i = 0; i < matching; ++i)
            if (!write (pipes [i], msg_))
                --i;
        int rc = msg_->close();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return;
    }

    //  Add matching-1 references to the message. We already hold one reference,
    //  that's why -1.
    msg_->add_refs ((int) matching - 1);

    //  Push copy of the message to each matching pipe.
    int failed = 0;
    for (pipes_t::size_type i = 0; i < matching; ++i)
        if (!write (pipes [i], msg_)) {
            --i;
            ++failed;
        }
    if (unlikely (failed))
        msg_->rm_refs (failed);

    //  Detach the original message from the data buffer. Note that we don't
    //  close the message. That's because we've already used all the references.
    int rc = msg_->init ();
    errno_assert (rc == 0);
}

bool xs::dist_t::has_out ()
{
    return true;
}

bool xs::dist_t::write (pipe_t *pipe_, msg_t *msg_)
{
    if (!pipe_->write (msg_)) {
        pipes.swap (pipes.index (pipe_), matching - 1);
        matching--;
        pipes.swap (pipes.index (pipe_), active - 1);
        active--;
        pipes.swap (active, eligible - 1);
        eligible--;
        return false;
    }
    if (!(msg_->flags () & msg_t::more))
        pipe_->flush ();
    return true;
}

