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
import java.util.Hashtable;

/**
 * Testcases for the native functions with prefix "hashtable" 
 * in the class NativeWrapper
 *
 * @see NativeWrapper
 * @see java.util.Hashtable
 */
public class HashtableTests extends TestCase
{
    public HashtableTests(String name)
	{
	    super(name);
	}

    public void testHashtableCreate()
	{
	    Hashtable hashtable=NativeWrapper.hashtableCreate();

	    //did this work?
	    assertTrue( hashtable != null );
	}

    public void testHashtablePut()
	{
	    Hashtable hashtable=new Hashtable();

	    // put one value
	    NativeWrapper.hashtablePut(hashtable, "key", "value");

	    // is it in there?
	    assertTrue( hashtable.size() == 1 );
	}

    public void testHashtablePutMany()
	{
	    Hashtable hashtable=new Hashtable();
	    int count=100;
	    int index;

	    for( index=0; index<count; index++ )
	    {
		String key = "key" + Integer.toString(index);
		Integer value = new Integer(index);

		NativeWrapper.hashtablePut(hashtable, key, value);
	    }

	    // are they all inside?
	    assertTrue( hashtable.size() == count );
	}

    /**
     * this a testcase to check if the functions
     * produces the expected exceptions if fed
     * with invalid values
     */
    public void testHashtablePutInvalid()
	{
	    Hashtable hashtable = new Hashtable();

	    try
	    {
		// try to add an invalid key
		NativeWrapper.hashtablePut(hashtable, null, "value");
		
		// still there? cant be
		assertTrue(false);
	    }
	    catch( NullPointerException e )
	    {
		// this should happen
		assertTrue(true);
	    }

	    try
	    {
		// now try to add an invalid value
		NativeWrapper.hashtablePut(hashtable, "key", null);

		// still there? error!
		assertTrue(false);
	    }
	    catch( NullPointerException e )
	    {
		// yes, good boy
		assertTrue(true);
	    }

	    try
	    {
		// now both key and value are invalid
		NativeWrapper.hashtablePut(hashtable, null, null);

		// wow. still there? no way!
		assertTrue(false);
	    }
	    catch( NullPointerException e )
	    {
		// yeaaa, great
		assertTrue(true);
	    }
	}
}





