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
import org.tigris.subversion.lib.Nodekind;

/**
 * Testcases for the native functions with prefix "nodekind" 
 * in the class NativeWrapper
 *
 * @see NativeWrapper
 * @see org.tigris.subversion.lib.Nodekind
 */
public class NodekindTests extends TestCase
{
    public NodekindTests(String name)
	{
	    super(name);
	}

    public void testNodekindCreate()
	{
	    Nodekind nodekind = NativeWrapper.nodekindCreate(0);
	    
	    assertNotNull( nodekind );
	}

    public void testNodekindCreateIntegrity()
	{
	    int kind = 55;
	    Nodekind nodekind = NativeWrapper.nodekindCreate(kind);

	    assertEquals( kind, nodekind.getKind() );
	}
}

