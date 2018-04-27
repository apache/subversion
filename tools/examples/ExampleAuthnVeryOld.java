/* Username/password prompt/save using very old org.tigris.subversion API.

   Compile against non-installed Subversion JavaHL build:

     javac -cp subversion/bindings/javahl/classes -d subversion/bindings/javahl/classes ExampleAuthnVeryOld.java

   Run:

     LD_LIBRARY_PATH=subversion/libsvn_auth_gnome_keyring/.libs java -cp subversion/bindings/javahl/classes -Djava.library.path=subversion/bindings/javahl/native/.libs ExampleAuthnVeryOld <URL> <config-dir>

 */
import org.tigris.subversion.javahl.*;
import java.io.Console;

public class ExampleAuthnVeryOld {

    protected static class MyAuthn {

      public static PromptUserPassword3 getAuthn() {
        return new MyUserPasswordCallback();
      }

      private static class MyUserPasswordCallback
      implements PromptUserPassword3 {

        private String _username = null;

        public String
        getUsername() {
          System.out.println("getUsername");
          return _username;
        }

        private String _password = null;

        public String
        getPassword() {
          System.out.println("getPassword");
          return _password;
        }

        public boolean
        userAllowedSave() {
          System.out.println("userAllowedSave");
          return true;
        }

        public boolean
        askYesNo(String realm, String question, boolean yesIsDefault) {
          System.out.println("askYesNo");
          System.out.print(question + " (y/n): ");
          String s = System.console().readLine();
          return s.equals("y") ? true : s.equals("") ? yesIsDefault : false;
        }

        public boolean
        prompt(String realm, String username, boolean maySave) {
          System.out.println("prompt");
          System.out.println("Realm: " + realm);
          String prompt;
          if (username == null) {
            System.out.print("Username: ");
            _username = System.console().readLine();
            prompt = "Password: ";
          } else {
            _username = username;
            prompt = "Password for " + username + ": ";
          }
          _password = new String(System.console().readPassword(prompt));
          return maySave;
        }

        public boolean
        prompt(String realm, String username) {
          System.out.println("prompt not implemented!");
          return true;
        }

        public String
        askQuestion(String realm,
                    String question,
                    boolean showAnswer,
                    boolean maySave) {
          System.out.println("askQuestion not implemented!");
          return null;
        }

        public String
        askQuestion(String realm, String question, boolean showAnswer) {
          System.out.println("askQuestion not implemented!");
          return null;
        }

        public int
        askTrustSSLServer(String info, boolean allowPermanently) {
          System.out.println("askTrustSSLServer not implemented!");
          return PromptUserPassword3.AcceptTemporary;
        }
      }
    }

  public static void main(String argv[]) {

    if (argv.length != 2) {
      System.err.println("usage: ExampleAuthnVeryOld <URL> <config-dir>");
      return;
    }
    SVNClientInterface client = new SVNClient();
    client.setPrompt(MyAuthn.getAuthn());
    try {
      client.setConfigDirectory(argv[1]);
      client.logMessages(argv[0], Revision.getInstance(0),
                         Revision.getInstance(0), false, false, 0);
    } catch (Exception e) {
      e.printStackTrace();
    }
  }
}
