package org.tigris.subversion.lib;

/**
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 *
 */

public final class Schedule 
{
    /**
     * IMPORTANT: KEEP THIS IN SYNC WITH THE
     * DEFINITION OF svn_wc_schedule_t
     */
    public final static int NORMAL=0;
    public final static int ADD=1;
    public final static int DELETE=2;
    public final static int REPLACE=3;

    private final int schedule;

    public Schedule(int _schedule) 
	{
	    super();
	    schedule = _schedule;
	}

    public Schedule(Schedule _schedule)
	{
	    this(_schedule.getSchedule());
	}

    public final int getSchedule()
	{
	    return schedule;
	}
}
