/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/barcontrol/src/BARServer.java,v $
* $Revision: 1.8 $
* $Author: torsten $
* Contents: BARControl (frontend for BAR)
* Systems: all
*
\***********************************************************************/

/****************************** Imports ********************************/
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.InputStreamReader;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.RuntimeException;
import java.net.ConnectException;
import java.net.InetSocketAddress;
import java.net.NoRouteToHostException;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.LinkedList;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/****************************** Classes ********************************/

/** Connection error
 */
class ConnectionError extends Error
{
  /** create new communication error
   * @param message message
   */
  ConnectionError(String message)
  {
    super(message);
  }
}

/** communication error
 */
class CommunicationError extends Error
{
  /** create new communication error
   * @param message message
   */
  CommunicationError(String message)
  {
    super(message);
  }
}

/** busy indicator
 */
class BARIndicator
{
  /** called when busy
   */
  public boolean busy(long n)
  {
    return true;
  }
}

/** BAR server
 */
class BARServer
{
  public static boolean debug = false;

  private static String             JAVA_SSL_KEY_FILE_NAME = "bar.jks";

  private static Socket             socket;
  private static BufferedWriter     output;
  private static BufferedReader     input;
  private static long               commandId;
  private static LinkedList<String> lines;

  /** connect to BAR server
   * @param hostname host name
   * @param port port number or 0
   * @param tlsPort TLS port number of 0
   * @param serverPassword server password
   */
  static void connect(String hostname, int port, int tlsPort, String serverPassword)
  {
    final int TIMEOUT = 20;

    assert hostname != null;
    assert (port != 0) || (tlsPort != 0);

    commandId = 0;

    // connect to server
    socket = null;
    String errorMessage = null;
    if ((socket == null) && (tlsPort != 0))
    {
      // get all possible bar.jks file names
      String[] javaSSLKeyFileNames = new String[]
      {
        JAVA_SSL_KEY_FILE_NAME,
        System.getProperty("user.home")+File.separator+".bar"+File.separator+JAVA_SSL_KEY_FILE_NAME,
        Config.CONFIG_DIR+File.separator+JAVA_SSL_KEY_FILE_NAME
      };

      // try to connect with key
      for (String javaSSLKeyFileName : javaSSLKeyFileNames)
      {
        File file = new File(javaSSLKeyFileName);
        if (file.exists() && file.isFile())
        {
          System.setProperty("javax.net.ssl.trustStore",javaSSLKeyFileName);
          try
          {
            SSLSocket        sslSocket;
            SSLSocketFactory sslSocketFactory;

            sslSocketFactory = (SSLSocketFactory)SSLSocketFactory.getDefault();
            sslSocket        = (SSLSocket)sslSocketFactory.createSocket(hostname,tlsPort);
            sslSocket.startHandshake();

            input  = new BufferedReader(new InputStreamReader(sslSocket.getInputStream()));
            output = new BufferedWriter(new OutputStreamWriter(sslSocket.getOutputStream()));

            socket = sslSocket;
            break;
          }
          catch (SocketTimeoutException exception)
          {
            errorMessage = "host '"+hostname+"' unreachable (error: "+exception.getMessage()+")";
          }
          catch (ConnectException exception)
          {
            errorMessage = "host '"+hostname+"' unreachable (error: "+exception.getMessage()+")";
          }
          catch (NoRouteToHostException exception)
          {
            errorMessage = "host '"+hostname+"' unreachable (no route to host)";
          }
          catch (UnknownHostException exception)
          {
            errorMessage = "unknown host '"+hostname+"'";
          }
          catch (RuntimeException exception)
          {
            errorMessage = exception.getMessage();
          }
          catch (Exception exception)
          {
    //        exception.printStackTrace();
            errorMessage = exception.getMessage();
          }
        }
      }
    }
    if ((socket == null) && (port != 0))
    {
      try
      {
        socket = new Socket(hostname,port);

        input  = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        output = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream()));
      }
      catch (SocketTimeoutException exception)
      {
        errorMessage = exception.getMessage();
      }
      catch (ConnectException exception)
      {
        errorMessage = exception.getMessage();
      }
      catch (NoRouteToHostException exception)
      {
        errorMessage = "host '"+hostname+"' unreachable (no route to host)";
      }
      catch (UnknownHostException exception)
      {
        errorMessage = "unknown host '"+hostname+"'";
      }
      catch (Exception exception)
      {
//        exception.printStackTrace();
        errorMessage = exception.getMessage();
      }
    }
    if (socket == null)
    {
      if   ((tlsPort != 0) || (port!= 0)) throw new ConnectionError(errorMessage);
      else                                throw new ConnectionError("no server ports specified");
    }

    // read session id
    byte sessionId[];
    try
    {
      String line;

      line = input.readLine();
      assert line != null;
      String data[] = line.split(" ",2);
      assert data.length == 2;
      assert data[0].equals("SESSION");
      sessionId = decodeHex(data[1]);
    }
    catch (IOException exception)
    {
      throw new Error("Network error (error: "+exception.getMessage()+")");
    }
//System.err.print("BARControl.java"+", "+682+": sessionId=");for (byte b : sessionId) { System.err.print(String.format("%02x",b & 0xFF)); }; System.err.println();

    // authorize
    try
    {
      byte authorizeData[] = new byte[sessionId.length];
      for (int z = 0; z < sessionId.length; z++)
      {
        authorizeData[z] = (byte)(((z < serverPassword.length())?(int)serverPassword.charAt(z):0)^(int)sessionId[z]);
      }
      commandId++;
      String command = Long.toString(commandId)+" AUTHORIZE "+encodeHex(authorizeData);
      output.write(command); output.write('\n'); output.flush();
//System.err.println("BARControl.java"+", "+230+": auto command "+command);

      String result = input.readLine();
      if (result == null)
      {
        throw new CommunicationError("No result from server");
      }
      String data[] = result.split(" ",4);
      if (data.length < 3)
      {
        throw new CommunicationError("Invalid response from server");
      }
      if (   (Integer.parseInt(data[0]) != commandId)
          || (Integer.parseInt(data[1]) != 1)
          || (Integer.parseInt(data[2]) != 0)
         )
      {
        throw new ConnectionError("Authorization fail");
      }
    }
    catch (IOException exception)
    {
      throw new CommunicationError("Network error (error: "+exception.getMessage()+")");
    }

    // get version
    try
    {
      String result;

      output.write("VERSION"); output.write('\n'); output.flush();
      result = input.readLine();
      if (result == null)
      {
        throw new CommunicationError("No result from server");
      }
      String data[] = result.split(" ",4);
      if (data.length < 3)
      {
        throw new CommunicationError("Invalid response from server");
      }
      if (   (Integer.parseInt(data[1]) != 1)
          || (Integer.parseInt(data[2]) != 0)
         )
      {
        throw new ConnectionError("Cannot connect to '"+hostname+"' (error: "+data[3]+")");
      }
    }
    catch (IOException exception)
    {
      throw new CommunicationError("Network error (error: "+exception.getMessage()+")");
    }
  }

  /** disconnect from BAR server
   */
  static void disconnect()
  {
    try
    {
      /* flush data (ignore errors) */
      executeCommand("JOB_FLUSH");

      /* close connection */
      input.close();
      output.close();
      socket.close();
    }
    catch (IOException exception)
    {
      // ignored
    }
  }

  /** abort command execution
   * @param result result (String[] or ArrayList)
   * @return error code
   */
  static int abort(Object result)
  {
    /* send abort command to current command */
    long abortCommandId = commandId;
    commandId++;
    String line = String.format("%d ABORT %d",commandId,abortCommandId);
    try
    {
      output.write(line); output.write('\n'); output.flush();
    }
    catch (IOException exception)
    {
      throw new CommunicationError("i/o error (error: "+exception.getMessage()+")");
    }
    if (debug) System.err.println("Network: sent '"+line+"'");

    /* set result */
    if      (result instanceof ArrayList)
    {
      ((ArrayList<String>)result).clear();
      ((ArrayList<String>)result).add("aborted");
    }
    else if (result instanceof String[])
    {
      ((String[])result)[0] = "aborted";
    }
    else
    {
      throw new Error("Invalid result data type");
    }

    return -1;
  }

  /** execute command
   * @param command command to send to BAR server
   * @param result result (String[] or ArrayList)
   * @return 0 or error code
   */
  static int executeCommand(String command, Object result, BARIndicator barIndicator)
  {
    String  line;
    long    n;
    boolean completedFlag;
    int     errorCode;

    synchronized(output)
    {
      if (barIndicator != null)
      {
        if (!barIndicator.busy(0)) return abort(result);
      }

      // send command
      commandId++;
      line = String.format("%d %s",commandId,command);
      try
      {
        output.write(line); output.write('\n'); output.flush();
      }
      catch (IOException exception)
      {
        throw new CommunicationError("i/o error (error: "+exception.getMessage()+")");
      }
      if (barIndicator != null)
      {
        if (!barIndicator.busy(0)) return abort(result);
      }
      if (debug) System.err.println("Network: sent '"+line+"'");

      // read buffer lines from list
  //???

      // clear result data
      if (result != null)
      {
        if      (result instanceof ArrayList)
        {
          ((ArrayList<String>)result).clear();
        }
        else if (result instanceof String[])
        {
          ((String[])result)[0] = "";
        }
        else
        {
          throw new Error("Invalid result data type");
        }
      }

      // read result
      n = 0;
      completedFlag = false;
      errorCode = -1;
      try
      {
        while (!completedFlag && (line = input.readLine()) != null)
        {
          // next line
          n++;
          if (barIndicator != null)
          {
            if (!barIndicator.busy(n)) return abort(result);
          }
          if (debug) System.err.println("Network: received '"+line+"'");

          // parse: line format <id> <error code> <completed flag> <data>
          String data[] = line.split(" ",4);
          if (data.length < 4)
          {
            throw new CommunicationError("malformed command result '"+line+"'");
          }
          try
          {
            if (Integer.parseInt(data[0]) == commandId)
            {
              // check if completed, check error code
              if (Integer.parseInt(data[1]) != 0)
              {
                errorCode = Integer.parseInt(data[2]);
  //              if (errorCode != 0) throw new CommunicationError(data[3]+" (error: "+errorCode+")");
                completedFlag = true;
              }

              /* get and store data */
              if (result != null)
              {
                // store data
                if      (result instanceof ArrayList)
                {
                  ((ArrayList<String>)result).add(data[3]);
                }
                else if (result instanceof String[])
                {
                  ((String[])result)[0] = data[3];
                }
              }
            }
            else
            {
//Dprintf.dprintf("commandId=%d: %s\n",commandId,line);
//              lines.add(line);
            }
          }
          catch (NumberFormatException exception)
          {
            throw new CommunicationError("malformed command result '"+line+"'");
          }
        }
      }
      catch (IOException exception)
      {
        throw new CommunicationError("Command fail (error: "+exception.getMessage()+")");
      }
    }

    return errorCode;
  }

  /** execute command
   * @param command command to send to BAR server
   * @param result result (String[] or ArrayList)
   * @return 0 or error code
   */
  static int executeCommand(String command, Object result)
  {
    return executeCommand(command,result,null);
  }

  /** execute command
   * @param command command to send to BAR server
   * @return 0 or error code
   */
  static int executeCommand(String command)
  {
    return executeCommand(command,null);
  }

  /** set boolean value on BAR server
   * @param jobId job id
   * @param name name of value
   * @param b value
   */
  static void set(String name, boolean b)
  {
    executeCommand("SET "+name+" "+(b?"yes":"no"));
  }

  /** set long value on BAR server
   * @param jobId job id
   * @param name name of value
   * @param n value
   */
  static void set(String name, long n)
  {
    executeCommand("SET "+name+" "+n);
  }

  /** set string value on BAR server
   * @param jobId job id
   * @param name name of value
   * @param s value
   */
  static void set(String name, String s)
  {
    executeCommand("SET "+name+" "+StringParser.escape(s));
  }

  /** get boolean value from BAR server
   * @param jobId job id
   * @param name name of value
   * @return value
   */
  static boolean getBooleanOption(int jobId, String name)
  {
    String[] result = new String[1];

    executeCommand("OPTION_GET "+jobId+" "+name,result);
    return    result[0].toLowerCase().equals("yes")
           || result[0].toLowerCase().equals("on")
           || result[0].equals("1");
  }

  /** get long value from BAR server
   * @param jobId job id
   * @param name name of value
   * @return value
   */
  static long getLongOption(int jobId, String name)
  {
    String[] result = new String[1];

    executeCommand("OPTION_GET "+jobId+" "+name,result);
    return Long.parseLong(result[0]);
  }

  /** get string value from BAR server
   * @param jobId job id
   * @param name name of value
   * @return value
   */
  static String getStringOption(int jobId, String name)
  {
    String[] result = new String[1];

    executeCommand("OPTION_GET "+jobId+" "+name,result);
    return StringParser.unescape(result[0]);
  }

  /** set boolean option value on BAR server
   * @param jobId job id
   * @param name option name of value
   * @param b value
   */
  static void setOption(int jobId, String name, boolean b)
  {
    executeCommand("OPTION_SET "+jobId+" "+name+" "+(b?"yes":"no"));
  }

  /** set long option value on BAR server
   * @param jobId job id
   * @param name option name of value
   * @param n value
   */
  static void setOption(int jobId, String name, long n)
  {
    executeCommand("OPTION_SET "+jobId+" "+name+" "+n);
  }

  /** set string option value on BAR server
   * @param jobId job id
   * @param name option name of value
   * @param s value
   */
  static void setOption(int jobId, String name, String s)
  {
    executeCommand("OPTION_SET "+jobId+" "+name+" "+StringParser.escape(s));
  }

  //-----------------------------------------------------------------------

  /** decode hex string
   * @param s hex string
   * @return bytes
   */
  private static byte[] decodeHex(String s)
  {
    byte data[] = new byte[s.length()/2];
    for (int z = 0; z < s.length()/2; z++)
    {
      data[z] = (byte)Integer.parseInt(s.substring(z*2,z*2+2),16);
    }

    return data;
  }

  /** encode hex string
   * @param data bytes
   * @return hex string
   */
  private static String encodeHex(byte data[])
  {
    StringBuffer stringBuffer = new StringBuffer(data.length*2);
    for (int z = 0; z < data.length; z++)
    {
      stringBuffer.append(String.format("%02x",(int)data[z] & 0xFF));
    }

    return stringBuffer.toString();
  }
}

/* end of file */
