/**
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

import junit.framework.*;
import java.util.Date;
import org.tigris.subversion.lib.*;
import java.util.Hashtable;

/**
 * Testcases for the native functions with prefix "entry" 
 * in the class NativeWrapper
 *
 * @see NativeWrapper
 * @see java.util.Date
 */
public class EntryTests extends TestCase
{
    public EntryTests(String name)
	{
	    super(name);
	}

    public void testEntryCreate()
	{
	    Entry entry=NativeWrapper.entryCreate();

	    assertNotNull( entry  );
	}

    public void testEntrySetUrl()
	{
	    Entry entry=new Entry();
	    String url="url";

	    NativeWrapper.entrySetUrl(entry, url);

	    assertEquals( url, entry.getUrl() );
	}

    public void testEntrySetRevision()
	{
	    Revision revision=new Revision(123456);
	    Entry entry = new Entry();

	    NativeWrapper.entrySetRevision(entry, revision);

	    assertEquals( revision, entry.getRevision() );
	}

    public void testEntrySetSchedule()
	{
	    Schedule schedule=new Schedule(Schedule.ADD);
	    Entry entry = new Entry();

	    NativeWrapper.entrySetSchedule(entry, schedule);

	    assertEquals( schedule, entry.getSchedule() );
	}

    public void testEntrySetConflicted()
	{
	    boolean conflicted = true;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetConflicted(entry, conflicted);

	    assertEquals( conflicted, entry.getConflicted() );
	}

    public void testEntrySetCopied()
	{
	    boolean copied = true;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetCopied(entry, copied);

	    assertEquals( copied, entry.getCopied() );
	}

    public void testEntrySetTexttime()
	{
	    Date date = new Date();
	    Entry entry = new Entry();

	    NativeWrapper.entrySetTexttime(entry, date);

	    assertEquals( date, entry.getTexttime() );
	}

    public void testEntrySetProptime()
	{
	    Date date = new Date();
	    Entry entry = new Entry();

	    NativeWrapper.entrySetProptime(entry, date);

	    assertEquals( date, entry.getProptime() );
	}

    public void testEntrySetAttributes()
	{
	    // TO VBE DONE
	    assertTrue(false);
	}

}




