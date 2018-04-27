/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 */

package org.apache.subversion.javahl.types;


/**
 * Object that describes a revision range
 */
public class RevisionRange implements Comparable<RevisionRange>, java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change made to
    // this class.  See the java documentation for when a change is incompatible.
    // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 2L;

    private Revision from;
    private Revision to;
    private boolean inheritable;

    /**
     * Creates a new instance.  Called by native library.
     */
    protected RevisionRange(long from, long to, boolean inheritable)
    {
        this.from = Revision.getInstance(from);
        this.to = Revision.getInstance(to);
        this.inheritable = inheritable;
    }

    /** @since 1.9 */
    public RevisionRange(Revision from, Revision to, boolean inheritable)
    {
        this.from = from;
        this.to = to;
        this.inheritable = inheritable;
    }

    public RevisionRange(Revision from, Revision to)
    {
        this.from = from;
        this.to = to;
        this.inheritable = true;
    }

    /**
     * Accepts a string in one of these forms: n m-n Parses the results into a
     * from and to revision
     * @param revisionElement revision range or single revision
     */
    public RevisionRange(String revisionElement)
    {
        super();
        if (revisionElement == null)
        {
            return;
        }

        this.inheritable = !revisionElement.endsWith("*");
        if (!this.inheritable)
            revisionElement =
                revisionElement.substring(0, revisionElement.length() - 1);

        int hyphen = revisionElement.indexOf('-');
        if (hyphen > 0)
        {
            try
            {
                long fromRev = Long
                        .parseLong(revisionElement.substring(0, hyphen));
                long toRev = Long.parseLong(revisionElement
                        .substring(hyphen + 1));
                this.from = new Revision.Number(fromRev);
                this.to = new Revision.Number(toRev);
            }
            catch (NumberFormatException e)
            {
                return;
            }

        }
        else
        {
            try
            {
                long revNum = Long.parseLong(revisionElement.trim());
                if (revNum <= 0)
                    return;
                this.to = new Revision.Number(revNum);
                this.from = new Revision.Number(revNum - 1);
            }
            catch (NumberFormatException e)
            {
                return;
            }
        }
    }

    public Revision getFromRevision()
    {
        return from;
    }

    public Revision getToRevision()
    {
        return to;
    }

    public boolean isInheritable()
    {
        return inheritable;
    }

    public String toString()
    {
        if (from != null && to != null)
        {
            String rep;

            if (from.getKind() == Revision.Kind.number
                && to.getKind() == Revision.Kind.number
                && (((Revision.Number)from).getNumber() + 1
                    == ((Revision.Number)to).getNumber()))
                rep = to.toString();
            else if (from.equals(to)) // Such ranges should never happen
                rep = from.toString();
            else
                rep = from.toString() + '-' + to.toString();
            if (!inheritable)
                return rep + '*';
            return rep;
        }
        return super.toString();
    }

    public static Long getRevisionAsLong(Revision rev)
    {
        long val = 0;
        if (rev != null && rev instanceof Revision.Number)
        {
            val = ((Revision.Number) rev).getNumber();
        }
        return new Long(val);
    }

    public int hashCode()
    {
        final int prime = 31;
        int result = (inheritable ? 1 : 2);
        result = prime * result + ((from == null) ? 0 : from.hashCode());
        result = prime * result + ((to == null) ? 0 : to.hashCode());
        return result;
    }

    /**
     * @param range The RevisionRange to compare this object to.
     */
    public boolean equals(Object range)
    {
        if (this == range)
            return true;
        if (!super.equals(range))
            return false;
        if (getClass() != range.getClass())
            return false;

        final RevisionRange other = (RevisionRange) range;

        if (from == null)
        {
            if (other.from != null)
                return false;
        }
        else if (!from.equals(other.from))
        {
            return false;
        }

        if (to == null)
        {
            if (other.to != null)
                return false;
        }
        else if (!to.equals(other.to))
        {
            return false;
        }

        return (inheritable == other.inheritable);
    }

    /**
     * <b>Note:</b> Explicitly ignores inheritable state.
     *
     * @param range The RevisionRange to compare this object to.
     */
    public int compareTo(RevisionRange range)
    {
        if (this == range)
            return 0;

        Revision other = (range).getFromRevision();
        return RevisionRange.getRevisionAsLong(this.getFromRevision())
            .compareTo(RevisionRange.getRevisionAsLong(other));
    }
}
