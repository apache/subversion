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

/**
 * Testcases for the native functions with prefix "misc" 
 * in the class NativeWrapper
 *
 * @see NativeWrapper
 */
public class MiscTests extends TestCase
{
    public MiscTests(String name)
	{
	    super(name);
	}

    public void testMiscThrowExceptionByName()
	{
	    String name="org/tigris/subversion/SubversionException";
	    String message="the answer is 42";

	    try
	    {
		NativeWrapper.miscThrowExceptionByName(name, message);

		fail("didnt throw an exception!");
	    }
	    catch( Exception e )
	    {
		// is this the exception we wanted?
		assertEquals( "org.tigris.subversion.SubversionException",
			      e.getClass().getName() );

		// does the message fit the one we stated?
		assertEquals( message, e.getMessage() );
	    }
	}
}
