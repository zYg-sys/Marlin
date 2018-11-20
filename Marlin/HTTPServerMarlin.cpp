/////////////////////////////////////////////////////////////////////////////////
//
// SourceFile: HTTPServerMarlin.cpp
//
// Marlin Server: Internet server/client
// 
// Copyright (c) 2015-2018 ir. W.E. Huisman
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
#include "stdafx.h"
#include "HTTPServerMarlin.h"
#include "HTTPSiteMarlin.h"
#include "AutoCritical.h"
#include "WebServiceServer.h"
#include "HTTPURLGroup.h"
#include "HTTPRequest.h"
#include "GetLastErrorAsString.h"
#include "ConvertWideString.h"
#include "WebSocketServer.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// Logging macro's
#define DETAILLOG1(text)          if(MUSTLOG(HLL_LOGGING) && m_log) { DetailLog (__FUNCTION__,LogType::LOG_INFO,text); }
#define DETAILLOGS(text,extra)    if(MUSTLOG(HLL_LOGGING) && m_log) { DetailLogS(__FUNCTION__,LogType::LOG_INFO,text,extra); }
#define DETAILLOGV(text,...)      if(MUSTLOG(HLL_LOGGING) && m_log) { DetailLogV(__FUNCTION__,LogType::LOG_INFO,text,__VA_ARGS__); }
#define WARNINGLOG(text,...)      if(MUSTLOG(HLL_LOGGING) && m_log) { DetailLogV(__FUNCTION__,LogType::LOG_WARN,text,__VA_ARGS__); }
#define ERRORLOG(code,text)       ErrorLog (__FUNCTION__,code,text)
#define HTTPERROR(code,text)      HTTPError(__FUNCTION__,code,text)

HTTPServerMarlin::HTTPServerMarlin(CString p_name)
                 :HTTPServer(p_name)
{
  // Default web.config
  m_webConfig = new WebConfig();
}

HTTPServerMarlin::~HTTPServerMarlin()
{
  // Cleanup the server objects
  Cleanup();
}

CString
HTTPServerMarlin::GetVersion()
{
  return CString(MARLIN_SERVER_VERSION " on Microsoft HTTP-Server API/2.0");
}

// Initialise a HTTP server and server-session
bool
HTTPServerMarlin::Initialise()
{
  AutoCritSec lock(&m_sitesLock);

  // See if there is something to do!
  if(m_initialized)
  {
    return true;
  }

  // STEP 1: LOGGING
  // Init logging, so we can complain about errors
  InitLogging();

  // STEP 2: CHECKING OF SETTINGS
  if(GeneralChecks() == false)
  {
    return false;
  }
  
  // STEP 3: INIT THE SERVER
  // Initialize HTTP library as-a-server.
  // Specifying VERSION_2 ensures we have the version with authentication and SSL/TLS!!!
  // Fails on Windows-XP and Server 2003 and lower
  ULONG retCode = HttpInitialize(HTTPAPI_VERSION_2,HTTP_INITIALIZE_SERVER,NULL);
  if(retCode != NO_ERROR)
  {
    ERRORLOG(retCode,"HTTP Initialize");
    return false;
  }
  DETAILLOG1("HTTPInitialize OK");

  // STEP 4: CREATE SERVER SESSION
  retCode = HttpCreateServerSession(HTTPAPI_VERSION_2,&m_session,0);
  if(retCode != NO_ERROR)
  {
    ERRORLOG(retCode,"CreateServerSession");
    return false;
  }
  DETAILLOGV("Serversession created: %I64X",m_session);

  // STEP 5: Create a request queue with NO name
  // Although we CAN create a name, it would mean a global object (and need to be unique)
  // So it's harder to create more than one server per machine
  retCode = HttpCreateRequestQueue(HTTPAPI_VERSION_2,NULL,NULL,0,&m_requestQueue);
  if(retCode != NO_ERROR)
  {
    ERRORLOG(retCode,"CreateRequestQueue");
    return false;
  }
  DETAILLOGV("Request queue created: %p",m_requestQueue);

  // STEP 6: SET UP FOR ASYNC I/O
  // Register the request queue for async I/O
  retCode = m_pool.AssociateIOHandle(m_requestQueue,(ULONG_PTR)HandleAsynchroneousIO);
  if(retCode != NO_ERROR)
  {
    ERRORLOG(retCode,"Associate request queue with the I/O completion port of the threadpool.");
    return false;
  }
  DETAILLOGV("Request queue registrated by the threadpool");

  // STEP 7: SET THE LENGTH OF THE BACKLOG QUEUE FOR INCOMING TRAFFIC
  // Overrides for the HTTP Site. Test min/max via SetQueueLength
  int queueLength = m_webConfig->GetParameterInteger("Server","QueueLength",m_queueLength);
  SetQueueLength(queueLength);
  // Set backlog queue: using HttpSetRequestQueueProperty
  retCode = HttpSetRequestQueueProperty(m_requestQueue
                                       ,HttpServerQueueLengthProperty
                                       ,&m_queueLength
                                       ,sizeof(ULONG)
                                       ,0
                                       ,NULL);
  if(retCode != NO_ERROR)
  {
    ERRORLOG(retCode,"HTTP backlog queue length NOT set!");
  }
  else
  {
    DETAILLOGV("HTTP backlog queue set to length: %lu",m_queueLength);
  }

  // STEP 8: SET VERBOSITY OF THE 503 SERVER ERROR
  // Set the 503-Verbosity: using HttpSetRequestQueueProperty
  DWORD verbosity = Http503ResponseVerbosityFull;
  retCode = HttpSetRequestQueueProperty(m_requestQueue
                                        ,HttpServer503VerbosityProperty
                                        ,&verbosity
                                        ,sizeof(DWORD)
                                        ,0
                                        ,NULL);
  if(retCode != NO_ERROR)
  {
    ERRORLOG(retCode,"Setting 503 verbosity property");
  }
  else
  {
    DETAILLOGV("HTTP 503-Error verbosity set to: %d",verbosity);
  }

  // STEP 9: Set the hard limits
  InitHardLimits();

  // STEP 10: Init the response headers to send
  InitHeaders();

  // STEP 11: Init the ThreadPool
  InitThreadPool();

  // We are airborne!
  return (m_initialized = true);
}

// Cleanup the server
void
HTTPServerMarlin::Cleanup()
{
  ULONG retCode;
  USES_CONVERSION;
  AutoCritSec lock1(&m_sitesLock);
  AutoCritSec lock2(&m_eventLock);

  // Remove all remaining sockets
  for(auto& it : m_sockets)
  {
    delete it.second;
  }
  m_sockets.clear();

  // Remove all event streams within the scope of the eventLock
  for(auto& it : m_eventStreams)
  {
    delete it.second;
  }
  m_eventStreams.clear();

  // Remove all services
  while(!m_allServices.empty())
  {
    ServiceMap::iterator it = m_allServices.begin();
    it->second->Stop();
  }

  // Remove all URL's in the sites map
  while(!m_allsites.empty())
  {
    SiteMap::iterator it = m_allsites.begin();
    it->second->StopSite(true);
  }

  // Try to stop and remove all groups
  for(auto group : m_urlGroups)
  {
    // Gracefully stop
    group->StopGroup();
    // And delete memory
    delete group;
  }
  m_urlGroups.clear();

  // Close the Request Queue handle.
  // Do this before removing the group
  if(m_requestQueue)
  {
    // using: HttpCloseRequestQueue
    retCode = HttpCloseRequestQueue(m_requestQueue);
    m_requestQueue = NULL;

    if(retCode == NO_ERROR)
    {
      DETAILLOG1("Closed the request queue");
    }
    else
    {
      ERRORLOG(retCode,"Cannot close the request queue");
    }
  }

  // Now close the session
  if(m_session)
  {
    // Using: HttpCloseServerSession
    retCode = HttpCloseServerSession(m_session);
    m_session = NULL;

    if(retCode == NO_ERROR)
    {
      DETAILLOG1("Closed the HTTP server session");
    }
    else
    {
      ERRORLOG(retCode,"Cannot close the HTTP server session");
    }
  }
  // Reset error state
  SetError(NO_ERROR);

  if(m_initialized)
  {
    // Call HttpTerminate.
    retCode = HttpTerminate(HTTP_INITIALIZE_SERVER,NULL);

    if(retCode == NO_ERROR)
    {
      DETAILLOG1("HTTP Server terminated OK");
    }
    else
    {
      ERRORLOG(retCode,"HTTP Server terminated with error");
    }
    m_initialized = false;
  }

  // Closing the logging file
  if(m_log && m_logOwner)
  {
    delete m_log;
    m_log = NULL;
  }
}

// Initialise general server header settings
void
HTTPServerMarlin::InitHeaders()
{
  CString name = m_webConfig->GetParameterString("Server","ServerName","");
  CString type = m_webConfig->GetParameterString("Server","TypeServerName","Hide");

  // Server name combo
  if(type.CompareNoCase("Microsoft")   == 0) m_sendHeader = SendHeader::HTTP_SH_MICROSOFT;
  if(type.CompareNoCase("Marlin")      == 0) m_sendHeader = SendHeader::HTTP_SH_MARLIN;
  if(type.CompareNoCase("Application") == 0) m_sendHeader = SendHeader::HTTP_SH_APPLICATION;
  if(type.CompareNoCase("Configured")  == 0) m_sendHeader = SendHeader::HTTP_SH_WEBCONFIG;
  if(type.CompareNoCase("Hide")        == 0) m_sendHeader = SendHeader::HTTP_SH_HIDESERVER;

  if(m_sendHeader == SendHeader::HTTP_SH_WEBCONFIG)
  {
    m_configServerName = name;
    DETAILLOGS("Server sends 'server' response header of type: ",name);
  }
  else
  {
    DETAILLOGS("Server sends 'server' response header: ",type);
  }
}

// Create a site to bind the traffic to
HTTPSite*
HTTPServerMarlin::CreateSite(PrefixType    p_type
                            ,bool          p_secure
                            ,int           p_port
                            ,CString       p_baseURL
                            ,bool          p_subsite  /* = false */
                            ,LPFN_CALLBACK p_callback /* = NULL  */)
{
  // USE OVERRIDES FROM WEBCONFIG 
  // BUT USE PROGRAM'S SETTINGS AS DEFAULT VALUES
  
  CString chanType;
  int     chanPort;
  CString chanBase;
  bool    chanSecure;

  // Changing the input to a name
  switch(p_type)
  {
    case PrefixType::URLPRE_Strong: chanType = "strong";   break;
    case PrefixType::URLPRE_Named:  chanType = "named";    break;
    case PrefixType::URLPRE_FQN:    chanType = "full";     break;
    case PrefixType::URLPRE_Address:chanType = "address";  break;
    case PrefixType::URLPRE_Weak:   chanType = "weak";     break;
  }

  // Getting the settings from the web.config, use parameters as defaults
  chanType      = m_webConfig->GetParameterString ("Server","ChannelType",chanType);
  chanSecure    = m_webConfig->GetParameterBoolean("Server","Secure",     p_secure);
  chanPort      = m_webConfig->GetParameterInteger("Server","Port",       p_port);
  chanBase      = m_webConfig->GetParameterString ("Server","BaseURL",    p_baseURL);

  // Recalculate the type
       if(chanType.CompareNoCase("strong")  == 0) p_type = PrefixType::URLPRE_Strong;
  else if(chanType.CompareNoCase("named")   == 0) p_type = PrefixType::URLPRE_Named;
  else if(chanType.CompareNoCase("full")    == 0) p_type = PrefixType::URLPRE_FQN;
  else if(chanType.CompareNoCase("address") == 0) p_type = PrefixType::URLPRE_Address;
  else if(chanType.CompareNoCase("weak")    == 0) p_type = PrefixType::URLPRE_Weak;

  // Only ports out of IANA/IETF range permitted!
  if(chanPort >= 1024 || 
     chanPort == INTERNET_DEFAULT_HTTP_PORT ||
     chanPort == INTERNET_DEFAULT_HTTPS_PORT )
  {
    p_port = chanPort;
  }
  // Only use other URL if one specified
  if(!chanBase.IsEmpty())
  {
    p_baseURL = chanBase;
  }

  // Create our URL prefix
  CString prefix = CreateURLPrefix(p_type,chanSecure,p_port,p_baseURL);
  if(!prefix.IsEmpty())
  {
    HTTPSite* mainSite = nullptr;
    if(p_subsite)
    {
      // Do sub-site lookup on receiving messages
      m_hasSubsites = true;
      // Finding the main site of this sub-site
      mainSite = FindHTTPSite(p_port,p_baseURL);
      if(mainSite == nullptr)
      {
        // No luck: No main site to register against
        CString message;
        message.Format("Tried to register a sub-site, without a main-site: %s",p_baseURL.GetString());
        ERRORLOG(ERROR_NOT_FOUND,message);
        return nullptr;
      }
    }
    // Create and register a URL
    // Remember URL Prefix strings, and create the site
    HTTPSiteMarlin* registeredSite = new HTTPSiteMarlin(this,p_port,p_baseURL,prefix,mainSite,p_callback);
    if(RegisterSite(registeredSite,prefix))
    {
      // Site created and registered
      return registeredSite;
    }
    delete registeredSite;
  }
  // No luck
  return nullptr;
}

// Delete a channel (from prefix OR base URL forms)
bool
HTTPServerMarlin::DeleteSite(int p_port,CString p_baseURL,bool p_force /*=false*/)
{
  AutoCritSec lock(&m_sitesLock);
  // Default result
  bool result = false;

  // Use counter
  m_counter.Start();

  CString search(MakeSiteRegistrationName(p_port,p_baseURL));
  SiteMap::iterator it = m_allsites.find(search);
  if(it != m_allsites.end())
  {
    // Finding our site
    HTTPSite* site = it->second;

    // See if other sites are dependent on this one
    if(p_force == false && site->GetIsSubsite() == false)
    {
      // Walk all sites, to see if subsites still dependent on this main site
      for(SiteMap::iterator fit = m_allsites.begin(); fit != m_allsites.end(); ++fit)
      {
        if(fit->second->GetMainSite() == site)
        {
          // Cannot delete this site, other sites are dependent on this one
          ERRORLOG(ERROR_ACCESS_DENIED,"Cannot remove site. Sub-sites still dependent on: " + p_baseURL);
          m_counter.Stop();
          return false;
        }
      }
    }
    // Remove a site from the URL group, so no 
    // HTTP calls will be accepted any more
    result = site->RemoveSiteFromGroup();
    // And remove from the site map
    if(result || p_force)
    {
      delete site;
      m_allsites.erase(it);
      result = true;
    }
  }
  // Use counter
  m_counter.Stop();

  return result;
}

// Find and make an URL group
HTTPURLGroup*
HTTPServerMarlin::FindUrlGroup(CString p_authName
                              ,ULONG   p_authScheme
                              ,bool    p_cache
                              ,CString p_realm
                              ,CString p_domain)
{
  // See if we already have a group of these combination
  // And if so: reuse that URL group
  for(auto group : m_urlGroups)
  {
    if(group->GetAuthenticationScheme()    == p_authScheme &&
       group->GetAuthenticationNtlmCache() == p_cache      &&
       group->GetAuthenticationRealm()     == p_realm      &&
       group->GetAuthenticationDomain()    == p_domain)
    {
      DETAILLOGS("URL Group recycled for authentication scheme: ",p_authName);
      return group;
    }
  }

  // No group found, create a new one
  HTTPURLGroup* group = new HTTPURLGroup(this,p_authName,p_authScheme,p_cache,p_realm,p_domain);

  // Start the group
  if(group->StartGroup())
  {
    m_urlGroups.push_back(group);
    int number = (int)m_urlGroups.size();
    DETAILLOGV("Created URL group [%d] for authentication: %s",number,p_authName.GetString());
  }
  else
  {
    ERRORLOG(ERROR_CANNOT_MAKE,"URL Group ***NOT*** created for authentication: " + p_authName);
    delete group;
    group = nullptr;
  }
  return group;
}

// Remove an URLGroup. Called by HTTPURLGroup itself
void
HTTPServerMarlin::RemoveURLGroup(HTTPURLGroup* p_group)
{
  for(URLGroupMap::iterator it = m_urlGroups.begin(); it != m_urlGroups.end(); ++it)
  {
    if(p_group == *it)
    {
      m_urlGroups.erase(it);
      return;
    }
  }
}

// New threads in the threadpool will start on listening to our HTTP queue
// and respond with to a HTTPRequest
/*static*/ void
StartHTTPRequest(void* p_argument)
{
  HTTPServerMarlin* server = reinterpret_cast<HTTPServerMarlin*>(p_argument);
  if(server)
  {
    HTTPRequest* request = new HTTPRequest(server);
    server->RegisterHTTPRequest(request);
    request->StartRequest();
  }
}

// Clean up our registered overlapping I/O object
// return true  -> Stay in the threadpool
// return false -> Thread is leaving threadpool
/*static*/ bool
CancelHTTPRequest(void* p_argument,bool p_stayInThePool,bool p_forcedAbort)
{
  // Check if we have an OVERLAPPED argument
  if(p_argument == INVALID_HANDLE_VALUE)
  {
    return p_stayInThePool;
  }

  OutstandingIO* outstanding = reinterpret_cast<OutstandingIO*>(p_argument);
  if(outstanding)
  {
    DWORD status = (DWORD)(outstanding->Internal & 0x0FFFF);
    if(status == 0 && outstanding->Offset == 0 && outstanding->OffsetHigh == 0)
    {
      HTTPRequest* request = reinterpret_cast<HTTPRequest*>(outstanding->m_request);
      if(request)
      {
        if((!request->GetIsActive() && !p_stayInThePool) || p_forcedAbort)
        {
          HTTPServer* server = request->GetHTTPServer();
          if(server)
          {
            server->UnRegisterHTTPRequest(request);
          }
          delete request;
          return false;
        }
        else if(p_stayInThePool && !request->GetIsActive())
        {
          // Start a new request, in case we completed the previous one
          request->StartRequest();
          return true;
        }
        else if(request->GetIsActive())
        {
          // Still processing, we must stay in the pool
          return true;
        }
      }
    }
  }
  return p_stayInThePool;
}

// Running the server on asynchronous I/O in the threadpool
void
HTTPServerMarlin::Run()
{
  // Do all initialization
  Initialise();

  // See if we are in a state to receive requests
  if(GetLastError() || !m_initialized)
  {
    ERRORLOG(ERROR_INVALID_PARAMETER,"RunHTTPServer called too early");
    return;
  }
  DETAILLOG1("HTTPServer initialised and ready to go!");

  // Check if all sites were properly started
  // Catches programmers who forget to call HTTPSite::StartSite()
  CheckSitesStarted();

  // Registering the init for the threadpool
  // This get new threads started on a HTTPRequest
  if(m_pool.SetThreadInitFunction(StartHTTPRequest,CancelHTTPRequest,(void*)this))
  {
    DETAILLOG1("HTTPServer registered for the threadpool init loop");
  }
  else
  {
    ERRORLOG(ERROR_INVALID_PARAMETER,"Threadpool cannot service our request queue!");
  }

  // STARTED OUR MAIN LOOP
  DETAILLOG1("HTTPServer entering main loop");
  m_running = true;

  // Starting the pool will trigger the starting of the reading threads
  // Because we registered our init function to start on a HTTPRequest
  m_pool.Run();
}

//////////////////////////////////////////////////////////////////////////
//
// STOPPING THE SERVER
//
//////////////////////////////////////////////////////////////////////////

void
HTTPServerMarlin::StopServer()
{
  AutoCritSec lock(&m_eventLock);
  DETAILLOG1("Received a StopServer request");

  // See if we are running at all
  if(m_running == false)
  {
    return;
  }

  // Try to remove all WebSockets
  while(!m_sockets.empty())
  {
    WebSocket* socket = m_sockets.begin()->second;
    socket->CloseSocket();
    UnRegisterWebSocket(socket);
  }
  // Try to remove all event streams
  for(auto& it : m_eventStreams)
  {
    // SEND OnClose event
    ServerEvent* event = new ServerEvent("close");
    SendEvent(it.second->m_port,it.second->m_baseURL,event);
  }
  // Try to remove all event streams
  while(!m_eventStreams.empty())
  {
    EventStream* stream = m_eventStreams.begin()->second;
    CloseEventStream(stream);
  }

  // See if we have a running server-push-event heartbeat monitor
  if(m_eventEvent)
  {
    // Explicitly pulse the event heartbeat monitor
    // this abandons the monitor in one go!
    DETAILLOG1("Abandon the server-push-events heartbeat monitor");
    SetEvent(m_eventEvent);
  }

  // mainloop will stop on next iteration
  m_running = false;

  // Cancel ALL (overlapped = null) outstanding I/O actions for the request queue
  ULONG result = CancelIoEx(m_requestQueue,NULL);
  if(result == NO_ERROR || result == ERROR_INVALID_FUNCTION)
  {
    // canceled, or no I/O to be canceled
    DETAILLOG1("HTTP Request queue cancelled all outstanding async I/O");
  }
  else
  {
    ERRORLOG(result,"Cannot cancel outstanding async I/O for the request queue");
  }

  // Shutdown the request queue canceling everything, and freeing the threads
  result = HttpShutdownRequestQueue(m_requestQueue);
  if(result == NO_ERROR)
  {
    DETAILLOG1("HTTP Request queue shutdown issued");
  }
  else
  {
    ERRORLOG(result,"Cannot shutdown the HTTP request queue");
  }
  m_requestQueue = NULL;

  // Cleanup the sites and groups
  Cleanup();
}

// Used for canceling a WebSocket for an event stream
void 
HTTPServerMarlin::CancelRequestStream(HTTP_OPAQUE_ID p_response)
{
  HTTPRequest* request = reinterpret_cast<HTTPRequest*>(p_response);

  // Cancel the outstanding request from the request queue
  if(request)
  {
    request->CancelRequest();
  }
}

// Create a new WebSocket in the subclass of our server
WebSocket*
HTTPServerMarlin::CreateWebSocket(CString p_uri)
{
  return new WebSocketServer(p_uri);
}

// Receive the WebSocket stream and pass on the the WebSocket
void
HTTPServerMarlin::ReceiveWebSocket(WebSocket* /*p_socket*/,HTTP_OPAQUE_ID /*p_request*/)
{
  // FOR WEBSOCKETS TO WORK ON THE STAND-ALONE MARLIN
  // IT NEEDS TO BE REWRIITEN TO DO ASYNC I/O THROUGHOUT THE SERVER!
  return;

}

bool       
HTTPServerMarlin::ReceiveIncomingRequest(HTTPMessage* /*p_message*/)
{
  ASSERT(false);
  ERRORLOG(ERROR_INVALID_PARAMETER,"ReceiveIncomingRequest in HTTPServerMarlin: Should never come to here!!");
  return false;
}


// Sending response for an incoming message
void       
HTTPServerMarlin::SendResponse(HTTPMessage* p_message)
{
  HTTPRequest* request = reinterpret_cast<HTTPRequest*>(p_message->GetRequestHandle());
  if(request)
  {
    // Reset the request handle here. The response will continue async from here
    // so the next handlers cannot find a request handle to answer to
    p_message->SetHasBeenAnswered();

    // Go send the response ASYNC
    request->StartResponse(p_message);
  }
}


//////////////////////////////////////////////////////////////////////////
//
// EVENT STREAMS
//
//////////////////////////////////////////////////////////////////////////

// Init the stream response
bool
HTTPServerMarlin::InitEventStream(EventStream& p_stream)
{
  HTTPRequest* request = reinterpret_cast<HTTPRequest*>(p_stream.m_requestID);
  if(request)
  {
    request->StartEventStreamResponse();
    return true;
  }
  return false;

}

// Sending a chunk to an event stream
bool
HTTPServerMarlin::SendResponseEventBuffer(HTTP_OPAQUE_ID p_requestID
                                         ,const char*    p_buffer
                                         ,size_t         p_length
                                         ,bool           p_continue /*=true*/)
{
  HTTPRequest* request = reinterpret_cast<HTTPRequest*>(p_requestID);
  if(request)
  {
    request->SendResponseStream(p_buffer,p_length,p_continue);
    return true;
  }
  return false;
}

// Cancel and close a WebSocket
bool
HTTPServerMarlin::FlushSocket(HTTP_OPAQUE_ID /*p_request*/)
{
  return true;
}
