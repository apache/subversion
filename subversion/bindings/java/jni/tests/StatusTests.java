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
import org.tigris.subversion.lib.*;

/**
 * Testcases for the native functions with prefix "status" 
 * in the class NativeWrapper
 *
 * @see NativeWrapper
 * @see org.tigris.subversion.lib
 */
public class StatusTests extends TestCase
{
    public StatusTests(String name)
	{
	    super(name);
	}

    public void testStatusCreate()
	{
	    Status status = NativeWrapper.statusCreate();

	    assertNotNull( status );
	}

    public void testStatusSetEntry()
	{
	    Status status = new Status();
	    Entry entry = new Entry();

	    NativeWrapper.statusSetEntry(status, entry);

	    // same thing?
	    assertEquals( entry, status.getEntry() );
	}

    public void testStatusSetTextStatus()
	{
	    Status status = new Status();
	    StatusKind text_status = new StatusKind(StatusKind.DELETED);

	    NativeWrapper.statusSetTextStatus(status, text_status);

	    assertEquals( text_status, status.getTextStatus() );
	}

    public void testStatusSetPropStatus()
	{
	    Status status = new Status();
	    StatusKind prop_status = new StatusKind(StatusKind.REPLACED);

	    NativeWrapper.statusSetPropStatus(status, prop_status);
	    
	    assertEquals( prop_status, status.getPropStatus() );
	}

    public void testStatusSetCopied()
	{
	    Status status = new Status();
	    boolean copied = true;

	    NativeWrapper.statusSetCopied(status, copied);

	    assertEquals( copied, status.getCopied() );
	}

    public void testStatusSetLocked()
	{
	    Status status = new Status();
	    boolean locked = true;

	    NativeWrapper.statusSetLocked(status, locked);

	    assertEquals( locked, status.getLocked() );
	}

    public void testStatusSetReposTextStatus()
	{
	    Status status = new Status();
	    StatusKind repos_text_status = new StatusKind(22);

	    NativeWrapper.statusSetReposTextStatus(status, 
						   repos_text_status);
	    
	    assertEquals( repos_text_status, 
			  status.getReposTextStatus() );
	}

    public void testStatusSetReposPropStatus()
	{
	    Status status = new Status();
	    StatusKind repos_prop_status = new StatusKind(511);

	    NativeWrapper.statusSetReposPropStatus(status,
						   repos_prop_status);
	    assertEquals( repos_prop_status, 
			  status.getReposPropStatus() );
	}
}



