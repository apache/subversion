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
import java.util.Vector;

/**
 * Units Tests for the native functions in
 * the class NativeWrapper.vector*
 *
 * @see NativeWrapper
 * @see java.util.Vector
 */
public class VectorTests extends TestCase 
{
    public VectorTests(String name)
	{
	    super(name);
	}

    /**
     * Test case that tries to create a Vector.
     *
     * @see NativeWrapper.vectorCreate
     */
    public void testVectorCreate()
	{
	    // this is a native call
	    Vector vector=NativeWrapper.vectorCreate();
	    assertTrue(vector != null);
	}

    /**
     * Test case that tries to add a String to a Vector
     *
     * @see java.lang.String
     * @see NativeWrapper.vectorAdd
     */
    public void testVectorAddString()
	{
	    Vector vector=new Vector();
	    String value=new String("secret question");

	    // second: add the string to the vector
	    NativeWrapper.vectorAdd(vector, value);

	    // now, check for the string
	    assertTrue(vector.firstElement() == value);
	}

    /**
     * Test case that tries to add a "null" value to a
     * Vector
     *
     * @see NativeWrapper.vectorAdd
     */
    public void testVectorAddNull()
	{
	    Vector vector=NativeWrapper.vectorCreate();
	    
	    NativeWrapper.vectorAdd(vector, null);
	    assertTrue(true);
	}

    /**
     * Test case that adds more than just one values to
     * a Vector and then checks afterwards if the count
     * is OK
     *
     * @see NativeWrapper.vectorAdd
     */
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

    /**
     * Test case that uses a null value as Vector parameter.
     * This should produce a NullPointer exception.
     *
     * @see NativeWrapper.vectorAdd
     */
    public void testVectorAddInvalid()
	{
	    try
	    {
		// this is an invalid call
		NativeWrapper.vectorAdd(null, null);
		
		// shouldnt be reached
		assertTrue(false);
	    }
	    catch( NullPointerException e )
	    {
		// a nullpointer exception is
		// what should happen
		assertTrue(true);
	    }
	}
}
