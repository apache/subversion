/**
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

    public void testEntryCreateFromSvnWcEntryT()
	{
	    Entry expectedEntry = new Entry();
	    Entry actualEntry;

	    /**
	     * set some values
	     */
	    expectedEntry.setRevision(new Revision(1245));
	    expectedEntry.setUrl("an url");
	    expectedEntry.setKind(new Nodekind(Nodekind.DIR));
	    expectedEntry.setSchedule(new Schedule(Schedule.ADD));
	    expectedEntry.setConflicted(true);
	    expectedEntry.setCopied(false);
	    expectedEntry.setTexttime(new Date());
	    expectedEntry.setProptime(new Date(0));
	    expectedEntry.setAttributes(new Hashtable());

	    /**
	     * now call the function
	     */

	    fail(" to be done");

	    actualEntry = 
		NativeWrapper.entryCreateFromSvnWcEntryT(expectedEntry);
	    
	    assertEquals( expectedEntry, actualEntry );
	}

    public void testEntrySetUrl()
	{
	    Entry entry=new Entry();
	    String url="url";

	    NativeWrapper.entrySetUrl(entry, url);

	    assertEquals( url, entry.getUrl() );
	}
    
    public void testEntryGetUrl()
	{
	    String expectedUrl = "this is the expected string";
	    Entry entry = new Entry();
	    entry.setUrl(expectedUrl);
	    String actualUrl = NativeWrapper.entryGetUrl(entry);

	    assertEquals( expectedUrl, actualUrl );
	}

    public void testEntrySetRevision()
	{
	    Revision revision=new Revision(123456);
	    Entry entry = new Entry();

	    NativeWrapper.entrySetRevision(entry, revision);

	    assertEquals( revision, entry.getRevision() );
	}

    public void testEntryGetRevision()
	{
	    Revision expectedRevision = new Revision(19700101);
	    Entry entry = new Entry();
	    Revision actualRevision;

	    entry.setRevision(expectedRevision);
	    
	    actualRevision = NativeWrapper.entryGetRevision(entry);
	    
	    assertEquals( expectedRevision, actualRevision );
	}

    public void testEntrySetKind()
	{
	    Nodekind nodekind=new Nodekind(Nodekind.DIR);
	    Entry entry = new Entry();

	    NativeWrapper.entrySetKind(entry, nodekind);

	    assertEquals( nodekind, entry.getKind() );
	}

     public void testEntryGetKind()
	{
	    Nodekind expectedNodekind = new Nodekind(Nodekind.FILE);
	    Entry entry = new Entry();
	    Nodekind actualNodekind;

	    entry.setKind(expectedNodekind);
	    
	    actualNodekind = NativeWrapper.entryGetKind(entry);
	    
	    assertEquals( expectedNodekind, actualNodekind );
	}

    public void testEntrySetSchedule()
	{
	    Schedule schedule=new Schedule(Schedule.ADD);
	    Entry entry = new Entry();

	    NativeWrapper.entrySetSchedule(entry, schedule);

	    assertEquals( schedule, entry.getSchedule() );
	}

    public void testEntryGetSchedule()
	{
	    Schedule expectedSchedule = new Schedule(Schedule.ADD);
	    Entry entry = new Entry();
	    Schedule actualSchedule;

	    entry.setSchedule(expectedSchedule);
	    
	    actualSchedule = NativeWrapper.entryGetSchedule(entry);
	    
	    assertEquals( expectedSchedule, actualSchedule );
	}

    public void testEntrySetConflicted()
	{
	    boolean conflicted = true;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetConflicted(entry, conflicted);

	    assertEquals( conflicted, entry.getConflicted() );
	}

    public void testEntryGetConflicted()
	{
	    boolean expectedConflicted = false;
	    Entry entry = new Entry();
	    boolean actualConflicted;

	    entry.setConflicted(expectedConflicted);
	    actualConflicted = NativeWrapper.entryGetConflicted(entry);

	    assertEquals( expectedConflicted, actualConflicted);
	}

    public void testEntrySetCopied()
	{
	    boolean copied = true;
	    Entry entry = new Entry();

	    NativeWrapper.entrySetCopied(entry, copied);

	    assertEquals( copied, entry.getCopied() );
	}

    public void testEntryGetCopied()
	{
	    boolean expectedCopied = true;
	    boolean actualCopied;
	    Entry entry = new Entry();
	    
	    entry.setCopied(expectedCopied);
	    actualCopied = NativeWrapper.entryGetCopied(entry);

	    assertEquals( expectedCopied, actualCopied );
	}
   
    public void testEntrySetTexttime()
	{
	    Date date = new Date();
	    Entry entry = new Entry();

	    NativeWrapper.entrySetTexttime(entry, date);

	    assertEquals( date, entry.getTexttime() );
	}

    public void testEntryGetTexttime()
	{
	    Date expectedDate = new Date();
	    Date actualDate;
	    Entry entry = new Entry();

	    entry.setTexttime(expectedDate);
	    actualDate = NativeWrapper.entryGetTexttime(entry);

	    assertEquals(expectedDate, actualDate);
	}

    public void testEntrySetProptime()
	{
	    Date date = new Date();
	    Entry entry = new Entry();

	    NativeWrapper.entrySetProptime(entry, date);

	    assertEquals( date, entry.getProptime() );
	}

    public void testEntryGetProptime()
	{
	    Date expectedDate = new Date();
	    Date actualDate;
	    Entry entry = new Entry();

	    entry.setProptime(expectedDate);
	    actualDate = NativeWrapper.entryGetProptime(entry);

	    assertEquals(expectedDate, actualDate);
	}

    public void testEntrySetAttributes()
	{
	    fail("To be done");
	}

    public void testEntryGetAttributes()
	{
	    fail("To be done");
	}
}




