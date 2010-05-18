/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

/**
 * Subversion diff summarization interface.  An implementation which
 * simply collects all summaries could look like:
 *
 * <blockquote><code><pre>
 * public class DiffSummaries extends ArrayList implements DiffSummaryReceiver
 * {
 *     public void onSummary(DiffSummary descriptor)
 *     {
 *         add(descriptor);
 *     }
 * }
 * </pre></code></blockquote>
 *
 * @since 1.5
 */
public interface DiffSummaryReceiver
{
    /**
     * Implement this interface to receive diff summaries from the
     * {@link SVNClientInterface#diffSummarize} API.
     *
     * @param descriptor A summary of the diff.
     */
    void onSummary(DiffSummary descriptor);
}
