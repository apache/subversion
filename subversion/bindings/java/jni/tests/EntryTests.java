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
	    //test noexistent
	    assertTrue(false);
	}

    public void testEntrySetUrl()
	{
	    Entry entry=new Entry();

	    NativeWrapper.entrySetUrl(entry, "url");
	    assertTrue(true);
	}

    public void testEntrySetRevision()
	{
	    long revision=123456;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetRevision(entry, revision);

	    assertTrue( revision == entry.getRevision() );
	}

    public void testEntrySetSchedule()
	{
	    int schedule=0;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetSchedule(entry, schedule);

	    assertTrue( schedule == entry.getSchedule() );
	}

    public void testEntrySetConflicted()
	{
	    boolean conflicted = true;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetConflicted(entry, conflicted);

	    assertTrue( conflicted == entry.getConflicted() );
	}

    public void testEntrySetCopied()
	{
	    boolean copied = true;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetCopied(entry, copied);

	    assertTrue( copied == entry.getCopied() );
	}

    public void testEntrySetTexttime()
	{
	    Date date = new Date();
	    Entry entry = new Entry();

	    NativeWrapper.entrySetTexttime(entry, date);

	    assertTrue( date.getTime() == entry.getTexttime().getTime() );
	}

    public void testEntrySetProptime()
	{
	    Date date = new Date();
	    Entry entry = new Entry();

	    NativeWrapper.entrySetProptime(entry, date);

	    assertTrue( date.getTime() == entry.getProptime().getTime() );
	}

    public void testEntrySetAttributes()
	{
	    // TO VBE DONE
	    assertTrue(false);
	}

}




