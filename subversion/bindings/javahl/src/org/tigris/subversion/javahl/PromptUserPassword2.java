/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
 * Partial interface for receiving callbacks for authentication.  New
 * applications should use PromptUserPassword3 instead.
 */
public interface PromptUserPassword2 extends PromptUserPassword
{
    /**
     * Reject the connection to the server.
     */
    public static final int Reject = 0;

    /**
     * Accept the connection to the server <i>once</i>.
     */
    public static final int AcceptTemporary = 1;

    /**
     * @deprecated Use the correctly spelled "AcceptTemporary"
     * constant instead.
     */
    public static final int AccecptTemporary = AcceptTemporary;

    /**
     * Accept the connection to the server <i>forever</i>.
     */
    public static final int AcceptPermanently = 2;

    /**
     * If there are problems with the certifcate of the SSL-server, this
     * callback will be used to deside if the connection will be used.
     * @param info              the probblems with the certificate.
     * @param allowPermanently  if AcceptPermantly is a legal answer
     * @return                  one of Reject/AcceptTemporary/AcceptPermanently
     */
    public int askTrustSSLServer(String info, boolean allowPermanently);
}
