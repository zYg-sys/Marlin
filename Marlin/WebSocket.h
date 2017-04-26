/////////////////////////////////////////////////////////////////////////////////
//
// SourceFile: WebSocket.h
//
// Marlin Server: Internet server/client
// 
// Copyright (c) 2017 ir. W.E. Huisman
// All rights reserved
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#pragma once
#include "HTTPClient.h"
#include <deque>
#include <map>

// Header and fragment sizes
constexpr auto WS_MIN_HEADER       =  2;  // Minimum header size in octets
constexpr auto WS_MAX_HEADER       = 14;  // Maximum header size in octets
constexpr auto WS_FRAGMENT_MINIMUM = (  1 * 4096) - WS_MAX_HEADER;  //  1 TCP/IP buffer
constexpr auto WS_FRAGMENT_DEFAULT = (  4 * 4096) - WS_MAX_HEADER;  // 16 KB buffer
constexpr auto WS_FRAGMENT_MAXIMUM = (256 * 4096) - WS_MAX_HEADER;  //  1 MB buffer
// Default keep alive time for a 'pong'
constexpr auto WS_KEEPALIVE_TIME   = 7000;      // 7 sec = 7000 milliseconds

// Forward declaration of our class
class WebSocket;
class HTTPServer;
class HTTPMessage;

enum class Opcode
{
  SO_CONTINU = 0    // Continuation frame of one of the other types
 ,SO_UTF8    = 1    // Payload is UTF-8 data
 ,SO_BINARY  = 2    // Payload is binary data
 ,SO_EXT3    = 3    // RESERVED for future use: PAYLOAD
 ,SO_EXT4    = 4    // RESERVED for future use: PAYLOAD
 ,SO_EXT5    = 5    // RESERVED for future use: PAYLOAD
 ,SO_EXT6    = 6    // RESERVED for future use: PAYLOAD
 ,SO_EXT7    = 7    // RESERVED for future use: PAYLOAD
 ,SO_CLOSE   = 8    // Close the socket frame
 ,SO_PING    = 9    // Ping the other side
 ,SO_PONG    = 10   // Pong on a ping, or a 'keepalive'
 ,SO_CTRL1   = 11   // RESERVED for future use: CONTROL frame
 ,SO_CTRL2   = 12   // RESERVED for future use: CONTROL frame
 ,SO_CTRL3   = 13   // RESERVED for future use: CONTROL frame
 ,SO_CTRL4   = 14   // RESERVED for future use: CONTROL frame
 ,SO_CTRL5   = 15   // RESERVED for future use: CONTROL frame
};

// Define OnClose status codes
#define WS_CLOSE_NORMAL       1000      // Normal closing of the connection
#define WS_CLOSE_GOINGAWAY    1001      // Going away. Closing webpage etc.
#define WS_CLOSE_BYERROR      1002      // Closing due to protocol error
#define WS_CLOSE_TERMINATE    1003      // Terminating (unacceptable data)
#define WS_CLOSE_RESERVED     1004      // Reserved for future use
#define WS_CLOSE_NOCLOSE      1005      // Internal error (no closing frame received)
#define WS_CLOSE_ABNORMAL     1006      // No closing frame, tcp/ip error?
#define WS_CLOSE_DATA         1007      // Abnormal data (no UTF-8 in UTF-8 frame)
#define WS_CLOSE_POLICY       1008      // Policy error, or extension error
#define WS_CLOSE_TOOBIG       1009      // Message is too big to handle
#define WS_CLOSE_NOEXTENSION  1010      // Not one of the expected extensions
#define WS_CLOSE_CONDITION    1011      // Internal server error
#define WS_CLOSE_SECURE       1015      // TLS handshake error (do not send!)

// OnClose status codes ranges
#define WS_CLOSE_MAX_PROTOCOL 2999      // 1000 - 2999 Reserved for WebSocket protocol
#define WS_CLOSE_MAX_IANA     3999      // 3000 - 3999 Reserved for IANA registration
#define WS_CLOSE_MAX_PRIVATE  4999      // 4000 - 4999 Usable by WebSocket applications

using int64 = __int64;

class RawFrame
{
public:
  RawFrame();
 ~RawFrame();

  bool    m_internal;       // For internal use or for the wire
  bool    m_isRead;         // Each fragement can be read once!
  // HEADER CONTENTS
  bool    m_finalFrame;     // true if final frame
  bool    m_reserved1;      // Extended protocol 1
  bool    m_reserved2;      // Extended protocol 2
  bool    m_reserved3;      // Extended protocol 3
  Opcode  m_opcode;         // Opcode of the frame
  bool    m_masked;         // Payload is masked
  int64   m_payloadLength;  // Length of the payload
  // HEADER END
  int     m_headerLength;   // Length of the header
  BYTE*   m_data;           // Pointer to the raw data (header + payload)
};

typedef void(*LPFN_SOCKETHANDLER)(WebSocket* p_event);

using FragmentStack = std::deque<RawFrame*>;

//////////////////////////////////////////////////////////////////////////
//
// The WebSocket class
//
//////////////////////////////////////////////////////////////////////////

class WebSocket
{
public:
  WebSocket(CString p_uri);
  virtual ~WebSocket();

  // FUNCTIONS

  // Open the socket
  virtual bool OpenSocket() = 0;
  // Close the socket
  virtual bool CloseSocket() = 0;
  // Write fragment to a WebSocket
  virtual bool WriteFragment(BYTE* p_buffer,int64 p_length,Opcode p_opcode,bool p_last = true) = 0;
  // Read a fragment from a WebSocket
  bool ReadFragment(BYTE*& p_buffer,int64& p_length,Opcode& p_opcode,bool& p_last);
  // Read the extensions of the current frame (Use before 'ReadFragment')
  bool ReadExtensions(Opcode& p_opcode,bool& p_extens1,bool& p_extens2,bool& p_extens3);
  // Amount of available fragments to read
  int  NumberOfFragments();
  // Close the socket with a closing frame
  bool CloseSocket(USHORT p_code,CString p_reason);
  // Decoded close connection (use in 'OnClose')
  bool GetCloseSocket(USHORT& p_code,CString& p_reason);
  // Socket still open?
  bool IsOpen();

  // HIGH LEVEL INTERFACE

  // Write as an UTF-8 string to the WebSocket
  bool WriteString(CString p_string);
  // Find UTF-8 string on the channel
  bool ReadString(CString& p_string);
  // Write as a binary object to the channel
  bool WriteObject(BYTE* p_buffer,int64 p_length);
  // Find a binary object on the channel
  bool ReadObject(BYTE*& p_buffer,int64& p_length);

  // DEFAULT HANDLERS
  void OnOpen();
  void OnMessage();
  void OnClose();

  // SETTERS

  // Setting the keep-alive interval for sending a 'pong'
  void SetKeepalive(ULONG p_miliseconds);
  // Max fragmentation size
  void SetFragmentSize(ULONG p_fragment);
  // Setting the protocols
  void SetProtocols(CString p_protocols);
  // Setting the extensions
  void SetExtensions(CString p_extensions);
  // Setting the logfile
  void SetLogfile(LogAnalysis* p_logfile);
  // Set logging to on or off
  void SetLogging(bool p_logging);
  // Set the OnOpen handler
  void SetOnOpen(LPFN_SOCKETHANDLER p_onOpen);
  // Set the OnMessage handler
  void SetOnMessage(LPFN_SOCKETHANDLER p_onMessage);
  // Set the OnClose handler
  void SetOnClose(LPFN_SOCKETHANDLER p_onClose);
  
  // GETTERS

  // Getting the URI
  CString GetURI()          { return m_uri;           };
  ULONG   GetKeepalive()    { return m_keepalive;     };
  ULONG   GetFragmentSize() { return m_fragmentsize;  };
  CString GetProtocols()    { return m_protocols;     };
  CString GetExtensions()   { return m_extensions;    };
  USHORT  GetClosingError() { return m_closingError;  };
  bool    GetDoLogging()    { return m_doLogging;     };
  LogAnalysis* GetLogfile() { return m_logfile;       };

  // BOTH HTTPClient and HTTPServer must have access to these functions

  // Generate a server key-answer
  CString ServerAcceptKey(CString p_clientKey);
  // Encode raw frame buffer
  bool    EncodeFramebuffer(RawFrame* p_frame,Opcode p_opcode,bool p_mask,BYTE* p_buffer,int64 p_length,bool p_last);
  // Decode raw frame buffer (only m_data is filled)
  bool    DecodeFrameBuffer(RawFrame* p_frame,int64 p_length);
  // Store incoming raw frame buffer
  bool    StoreFrameBuffer(RawFrame* p_frame);
  // Send a 'ping' to see if other side still there
  bool    SendPing(bool p_waitForPong = false);
  // Send a 'pong' as an answer on a 'ping'
  void    SendPong(RawFrame* p_ping);

protected:
  // Completely close the connection
  void    Close();
  // Decode the incoming closing fragement before we call 'OnClose'
  void    DecodeCloseFragment(RawFrame* p_frame);

  // GENERAL SOCKET DATA
  CString m_uri;                      // ws[s]://resource URI for the socket
  bool    m_open          { false };  // WebSocket is opened and alive
  ULONG   m_keepalive;                // Keep alive time of the socket
  ULONG   m_fragmentsize;             // Max fragment size
  CString m_protocols;                // WebSocket main protocols
  CString m_extensions;               // Extensions in 1,2,3 fields
  USHORT  m_closingError  { 0     };  // Error on closing
  CString m_closing;                  // Closing error text
  HANDLE  m_pingEvent;                // The ping/pong event
  ULONG   m_pingTimeout   { 5000  };  // How long we wait for a pong after a ping
  bool    m_pongSeen      { false };  // Seen a pong for a ping
  bool    m_doLogging     { false };  // Do we do the logging?
  // Complex objects
  FragmentStack m_stack;              // Incoming fragments
  LogAnalysis*  m_logfile {nullptr};  // Connected to this logfile
  // Handlers
  LPFN_SOCKETHANDLER m_onopen;        // OnOpen    handler
  LPFN_SOCKETHANDLER m_onmessage;     // OnMessage handler
  LPFN_SOCKETHANDLER m_onclose;       // OnClose   handler
  // Synchronization for the fragment stack
  CRITICAL_SECTION   m_lock;
};

inline bool
WebSocket::IsOpen()
{
  return m_open;
}

inline void
WebSocket::SetKeepalive(ULONG p_miliseconds)
{
  m_keepalive = p_miliseconds;
}

inline void
WebSocket::SetFragmentSize(ULONG p_fragment)
{
  m_fragmentsize = p_fragment;
}

inline void 
WebSocket::SetProtocols(CString p_protocols)
{
  m_protocols = p_protocols;
}

inline void 
WebSocket::SetExtensions(CString p_extensions)
{
  m_extensions = p_extensions;
}

inline void 
WebSocket::SetLogfile(LogAnalysis* p_logfile)
{
  m_logfile = p_logfile;
}

inline void
WebSocket::SetLogging(bool p_logging)
{
  m_doLogging = p_logging;
}

inline void 
WebSocket::SetOnOpen(LPFN_SOCKETHANDLER p_onOpen)
{
  m_onopen = p_onOpen;
}

inline void 
WebSocket::SetOnMessage(LPFN_SOCKETHANDLER p_onMessage)
{
  m_onmessage = p_onMessage;
}

inline void 
WebSocket::SetOnClose(LPFN_SOCKETHANDLER p_onClose)
{
  m_onclose = p_onClose;
}

//////////////////////////////////////////////////////////////////////////
//
// SERVER WebSocket
//
//////////////////////////////////////////////////////////////////////////

using SocketParams = std::map<CString,CString>;

class ServerWebSocket: public WebSocket
{
public:
  ServerWebSocket(CString p_uri);
  virtual ~ServerWebSocket();

  // FUNCTIONS

  // Open the socket
  virtual bool OpenSocket();
  // Close the socket
  virtual bool CloseSocket();
  // Write fragment to a WebSocket
  virtual bool WriteFragment(BYTE* p_buffer,int64 p_length,Opcode  p_opcode,bool  p_last = true);
  // Perform the server handshake
  bool    ServerHandshake(HTTPMessage* p_message);
  // Add a URI parameter
  void    AddParameter(CString p_name,CString p_value);
  // Find a URI parameter
  CString GetParameter(CString p_name);
  // Register the server request for sending info
  void    RegisterServerRequest(HTTPServer* p_server,HTTP_REQUEST_ID p_request);

protected:
  SocketParams    m_parameters;
  HTTPServer*     m_server  { nullptr };
  HTTP_REQUEST_ID m_request { 0 };
};

inline void
ServerWebSocket::RegisterServerRequest(HTTPServer* p_server,HTTP_REQUEST_ID p_request)
{
  m_server  = p_server;
  m_request = p_request;
}

//////////////////////////////////////////////////////////////////////////
//
// CLIENT WebSocket
//
//////////////////////////////////////////////////////////////////////////

class ClientWebSocket: public WebSocket
{
public:
  ClientWebSocket(CString p_uri);
  virtual ~ClientWebSocket();

  // FUNCTIONS

  // Open the socket
  virtual bool OpenSocket();
  // Close the socket
  virtual bool CloseSocket();
  // Write fragment to a WebSocket
  virtual bool WriteFragment(BYTE* p_buffer,int64 p_length,Opcode  p_opcode,bool  p_last = true);

  // GETTERS

  // Getting a handle on the client to set parameters
  HTTPClient& GetHTTPClient();

protected:
  CString GenerateKey();

  HTTPClient m_HTTPClient;
  CString    m_socketKey;   // Given at the start
};

inline HTTPClient& 
ClientWebSocket::GetHTTPClient()
{
  return m_HTTPClient;
}
