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
import java.util.Date;

/**
 * Testcases for the native functions with prefix "date" 
 * in the class NativeWrapper
 *
 * @see NativeWrapper
 * @see java.util.Date
 */
public class DateTests extends TestCase
{
    public DateTests(String name)
	{
	    super(name);
	}

    /**
     * Test case to make sure the function returns
     * a valid date instance
     *
     * @see NativeWrapper.dateCreate
     */
    public void testDateCreateValidDate()
	{
	    Date date=NativeWrapper.dateCreate(0);
	    assertNotNull( date );
	}

    /**
     * Test case that to make sure, dateCreate really
     * returns a Date instance, that truely means
     * the same that the value means
     *
     * @see NativeWrapper.dateCreate
     */
    public void testDateCreateIntegrity()
	{
	    // create a new date
	    Date now=new Date();
	    // retrieve the long value with millisecs
	    long time=now.getTime();
	    // call dateAprToJ
	    Date date=NativeWrapper.dateCreate(time);

	    //Now lets see. Do the dates match?
	    assertEquals( time, date.getTime() );
	}
    
    /**
     * test case to ensure the method creates a valid date
     */
    public void testDateCreateFromAprTimeT()
	{
	    Date date = NativeWrapper.dateCreateFromAprTimeT(0);

	    assertNotNull(date);
	}

    /**
     * test case to ensure the conversions produce
     * valid results (microseconds -> milliseconds)
     */
    public void testDateCreateFromAprTimeTIntegrity()
	{
	    Date now = new Date();
	    long milliseconds = now.getTime();
	    long microseconds = milliseconds * 1000;
	    Date date = NativeWrapper.dateCreateFromAprTimeT(microseconds);
	
	    assertEquals(milliseconds, date.getTime());
	}
}

