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
 * The original interface for receiving callbacks for authentication.
 * Consider this code deprecated -- new applications should use
 * PromptUserPassword3 instead.
 */
public interface PromptUserPassword
{
    /**
     * Ask the user for username and password
     * The entered username/password is retrieved by the getUsername
     * getPasswort methods.
     *
     * @param realm     for which server realm this information is requested.
     * @param username  the default username
     * @return Whether the prompt for authentication credentials was
     * successful (e.g. in a GUI application whether the dialog box
     * was canceled).
     */
    public boolean prompt(String realm, String username);

    /**
     * ask the user a yes/no question
     * @param realm         for which server realm this information is requested.
     * @param question      question to be asked
     * @param yesIsDefault  if yes should be the default
     * @return              the answer
     */
    public boolean askYesNo(String realm, String question,
                            boolean yesIsDefault);

    /**
     * ask the user a question where she answers with a text.
     * @param realm         for which server realm this information is requested.
     * @param question      question to be asked
     * @param showAnswer    if the answer is shown or hidden
     * @return              the entered text or null if canceled
     */
    public String askQuestion(String realm, String question,
                              boolean showAnswer);

    /**
     * retrieve the username entered during the prompt call
     * @return the username
     */
    public String getUsername();

    /**
     * retrieve the password entered during the prompt call
     * @return the password
     */
    public String getPassword();
}
