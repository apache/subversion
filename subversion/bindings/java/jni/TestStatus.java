import org.tigris.subversion.lib.*;

public class TestStatus extends Object
{
  public static void main(String[] args)
  {
    ClientImpl client;

    try 
      {
        System.out.println("trying to create ClientImpl");
        client = new ClientImpl();
 	
        System.out.println("sucess\nnow for the Test");
        client.status("Test", true, false, false);

        System.out.println("success again!");
      }
    catch(Exception e)
      {
        e.printStackTrace();
      }

  }

}
