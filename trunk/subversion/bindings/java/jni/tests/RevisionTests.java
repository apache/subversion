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

import junit.framework.*;
import org.tigris.subversion.lib.Revision;

/**
 * Testcases for the native functions with prefix "revision" 
 * in the class NativeWrapper
 *
 * @see NativeWrapper
 * @see org.tigris.subversion.lib.Revision
 */
public class RevisionTests extends TestCase
{
    public RevisionTests(String name)
	{
	    super(name);
	}

    public void testRevisionCreate()
	{
	    Revision revision = NativeWrapper.revisionCreate(0);
	    
	    assertNotNull( revision );
	}

    public void testRevisionCreateIntegrity()
	{
	    long rev = 55;
	    Revision revision = NativeWrapper.revisionCreate(rev);

	    assertEquals( rev, revision.toLong() );
	}
}

