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
import java.util.Vector;

public class VectorTests extends TestCase 
{
    public VectorTests(String name)
	{
	    super(name);
	}

    public void testFail()
	{
	    assertTrue(true);
	}
    public void testCreateVector()
	{
	    // this is a native call
	    Vector vector=NativeWrapper.vectorCreate();
	    assertTrue(vector != null);
	}
    public void testVectorAddString()
	{
	    // first: create a vector
	    Vector vector=NativeWrapper.vectorCreate();
	    String value=new String("secret question");

	    // second: add the string to the vector
	    NativeWrapper.vectorAdd(vector, value);

	    // now, check for the string
	    assertTrue(vector.firstElement() == value);
	}
    public void testVectorAddNull()
	{
	    Vector vector=NativeWrapper.vectorCreate();
	    
	    NativeWrapper.vectorAdd(vector, null);
	    assertTrue(true);
	}
    public void TestVectorAddMany()
	{
	    Vector vector=NativeWrapper.vectorCreate();
	    int index;
	    final int count=100;

	    for( index=0; index<count; index++ )
	    {
		String value = 
		    new String(
			"String " + Integer.toString(count)) ;

		NativeWrapper.vectorAdd(vector, value);
	    }

	    assertTrue(count == vector.size());
	}
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */
