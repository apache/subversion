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
import org.tigris.subversion.SubversionException;
import org.tigris.subversion.lib.*;
import java.util.Vector;

/**
 * JUnit Testcases 
 *
 * @see NativeWrapper
 * @see org.tigris.subversion.lib
 */
public class ClientTests extends TestCase
{
    public ClientTests(String name)
	{
	    super(name);
	}

    public void testClient()
	{
	    Client client= Factory.getClient();

	    assertNotNull( client );
	}

    /**
     * testcase that tries to run a simple "status"
     * request.
     */
    public void testClientStatusSimple() throws SubversionException
	{
	    Client client = Factory.getClient();
	    String path = ".";
	    boolean descend = false;
	    boolean get_all = false;
	    boolean update = false;
	    Vector result;

	    result = client.status(path, descend, get_all, update);
	       
	    assertNotNull( result );
	}
	    
}



