/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * Created by IntelliJ IDEA.
 * User: patrick
 * Date: Feb 3, 2003
 * Time: 6:33:11 AM
 * To change this template use Options | File Templates.
 */
public interface PromptUserPassword
{
    public boolean prompt(String realm, String username);
    public boolean askYesNo(String realm, String question, boolean yesIsDefault);
    public String askQuestion(String realm, String question, boolean showAnswer);
    public String getUsername();
    public String getPassword();
}
