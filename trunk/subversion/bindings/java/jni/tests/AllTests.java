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
 * JUnits tests for the Java subversion binding helper functions
 */
public class AllTests {

    public static void main( String [] args )
	{
	    junit.textui.TestRunner.run( suite() );
	}

    public static Test suite( )
	{
	    TestSuite suite = new TestSuite(
		"All JUnit tests for the Java Subversion binding");

	    //add tests here
	    suite.addTestSuite( DateTests.class );
	    suite.addTestSuite( EntryTests.class );
	    suite.addTestSuite( VectorTests.class );
	    suite.addTestSuite( HashtableTests.class );
	    suite.addTestSuite( MiscTests.class );
	    suite.addTestSuite( StatusTests.class );
	    suite.addTestSuite( NodekindTests.class );
	    suite.addTestSuite( RevisionTests.class );
	    suite.addTestSuite( StatusKindTests.class );
	    suite.addTestSuite( ClientTests.class );

	    return suite;
	}
}
