/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @endcopyright
 */

package org.tigris.subversion.javahl;

import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.util.TimeZone;

/**
 * Holds date for a log message.  This class maintains
 * the time to the microsecond and is not lossy.
 *
 * @since 1.5
 */
public class LogDate implements java.io.Serializable
{
    private static final long serialVersionUID = 1L;
    private static final DateFormat formatter = new SimpleDateFormat(
            "yyyy-MM-dd'T'HH:mm:ss.SSS z");
    private static final TimeZone UTC = TimeZone.getTimeZone("UTC");

    private final long timeMicros;
    private final String cachedString;
    private final Calendar cachedDate;

    public LogDate(String datestr) throws ParseException
    {
        if (datestr == null || datestr.length() != 27 || datestr.charAt(26) != 'Z')
        {
            throw new ParseException("String is not a valid Subversion date", 0);
        }
        Date date = formatter.parse(datestr.substring(0, 23) + " UTC");
        this.cachedString = datestr;
        cachedDate = Calendar.getInstance(UTC);
        cachedDate.setTime(date);
        timeMicros = cachedDate.getTimeInMillis() * 1000
                        + Integer.parseInt(datestr.substring(23, 26));
    }

    /**
     * Returns the time of the commit in microseconds
     * @return the time of the commit measured in the number of
     *         microseconds since 00:00:00 January 1, 1970 UTC
     */
    public long getTimeMicros()
    {
        return timeMicros;
    }

    /**
     * Returns the time of the commit in milliseconds
     * @return the time of the commit measured in the number of
     *         milliseconds since 00:00:00 January 1, 1970 UTC
     */
    public long getTimeMillis()
    {
        return cachedDate.getTimeInMillis();
    }

    /**
     * Returns the time of the commit as Calendar
     * @return the time of the commit as java.util.Calendar
     */
    public Calendar getCalender()
    {
        return cachedDate;
    }

    /**
     * Returns the date of the commit
     * @return the time of the commit as java.util.Date
     */
    public Date getDate()
    {
        return cachedDate.getTime();
    }

    public String toString()
    {
         return cachedString;
    }

    public int hashCode()
    {
        final int prime = 31;
        int result = 1;
        result = prime * result + (int) (timeMicros ^ (timeMicros >>> 32));
        return result;
    }

    public boolean equals(Object obj)
    {
        if (this == obj)
            return true;
        if (obj == null)
            return false;
        if (getClass() != obj.getClass())
            return false;
        final LogDate other = (LogDate) obj;
        if (timeMicros != other.getTimeMicros())
            return false;
        return true;
    }

}
