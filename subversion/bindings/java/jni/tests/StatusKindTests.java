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
 * JUnit Testcases
 *
 * @see org.tigris.subversion.lib.StatusKind
 */
public class StatusKindTests extends TestCase
{
    public StatusKindTests(String name)
	{
	    super(name);
	}

    public void testStatuskindCreate()
	{
	    int kind = StatusKind.NONE;
	    StatusKind statusKind = 
		NativeWrapper.statuskindCreate(kind);

	    assertNotNull( statusKind );
	}

    public void testStatuskindCreateIntegrity()
	{
	    int kind = StatusKind.MERGED;
	    StatusKind statusKind = 
		NativeWrapper.statuskindCreate(kind);

	    assertEquals( kind, statusKind.getKind() );
	}
}



