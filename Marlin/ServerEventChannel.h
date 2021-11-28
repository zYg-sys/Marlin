/////////////////////////////////////////////////////////////////////////////////
//
// SourceFile: ServerEventChannel.h
//
// Marlin Server: Internet server/client
// 
// Copyright (c) 2014-2021 ir. W.E. Huisman
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
#include "ThreadPool.h"
#include "LongTermEvent.h"
#include <deque>
#include <map>

class HTTPServer;
class HTTPMessage;
class SOAPMessage;
class WebSocket;
class EventStream;
class ServerEventDriver;

// Types of channel connections
enum class EventDriverType
{
  EDT_NotConnected = 0
 ,EDT_Sockets      = 1
 ,EDT_ServerEvents = 2
 ,EDT_LongPolling  = 3
};

using EventQueue = std::deque<LTEvent*>;
using AllStreams = std::map<CString,EventStream*>;
using AllSockets = std::map<CString,WebSocket*>;
using SocketOpen = std::map<WebSocket*,bool>;

class ServerEventChannel
{
public:
  ServerEventChannel(ServerEventDriver* p_driver
                    ,int     p_channel
                    ,CString p_sesionName
                    ,CString p_cookie
                    ,CString p_token);
 ~ServerEventChannel();

  // Send from this channel (if possible)
  int  SendChannel();
  // Process the receiving part of the queue
  int  Receiving();
  // Post a new event, giving a new event numerator
  int  PostEvent(CString p_payload,EvtType type = EvtType::EV_Message);
  // Closing an event channel
  void CloseChannel();

  // Register new incoming channels
  bool RegisterNewSocket(HTTPMessage* p_message,WebSocket*   p_socket,bool p_check = false);
  bool RegisterNewStream(HTTPMessage* p_message,EventStream* p_stream,bool p_check = false);
  bool HandleLongPolling(SOAPMessage* p_message,bool p_check = false);
  // Change event policy. The callback will receive the LTEVENT as the void* parameter
  bool ChangeEventPolicy(EVChannelPolicy p_policy,LPFN_CALLBACK p_application,UINT64 p_data);

  // GETTERS
  int     GetChannel()      { return m_channel; }
  CString GetChannelName()  { return m_name;    }
  CString GetToken()        { return m_token;   }
  CString GetCookieToken();
  int     GetQueueCount();
  int     GetClientCount();
  // Current active type
  EventDriverType GetDriverType() { return m_current; }

  // To be called by the socket handlers only!
  void    OnOpen   (CString p_message);
  void    OnMessage(CString p_message);
  void    OnError  (CString p_message);
  void    OnClose  (CString p_message);
  void    OnBinary (void* p_data,DWORD p_length);
  void    OnOpenSocket (WebSocket* p_socket);
  void    OnCloseSocket(WebSocket* p_socket);

private:
  void CloseSocket(WebSocket* p_socket);
  void CloseStream(EventStream* p_stream);
  int  SendQueueToSocket();
  int  SendQueueToStream();
  int  LogLongPolling();
  int  LogNotConnected();
  bool RemoveEvents(int p_number);

  // DATA
  CString             m_name;
  CString             m_cookie;
  CString             m_token;
  int                 m_channel     { 0 };
  bool                m_active      { false   };
  EVChannelPolicy     m_policy      { EVChannelPolicy::DP_SureDelivery   };
  EventDriverType     m_current     { EventDriverType::EDT_NotConnected };
  AllStreams          m_streams;
  AllSockets          m_sockets;
  SocketOpen          m_opened;
  // Application we are working for
  LPFN_CALLBACK       m_application { nullptr };
  UINT64              m_appData     { 0L      };
  // All events to be sent
  EventQueue          m_outQueue;
  int                 m_maxNumber   { 0 };
  int                 m_minNumber   { 0 };
  INT64               m_lastSending { 0 };
  // All incoming events from the client
  EventQueue          m_inQueue;
  bool                m_openSeen    { false };
  bool                m_closeSeen   { false };
  // We belong to this driver
  ServerEventDriver*  m_driver      { nullptr };
  HTTPServer*         m_server      { nullptr };
  // LOCKING
  CRITICAL_SECTION    m_lock;
};
