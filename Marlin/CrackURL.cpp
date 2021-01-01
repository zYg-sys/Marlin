/////////////////////////////////////////////////////////////////////////////////
//
// SourceFile: CrackURL.cpp
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

//////////////////////////////////////////////////////////////////////////
//
// CRACK / SPLIT A HTTP URI (URL in HTTP) IN IT'S COMPOSING PARTS
//
//
//  foo://username:password@example.com:8042/over/there/index.dtb?type=animal&name=white%20narwhal#nose
//  \_/   \_______________/ \_________/ \__/            \___/ \_/ \______________________________/ \__/
//   |           |               |       |                |    |            |                       |
//   |       userinfo         hostname  port              |    |          query                   anchor
//   |    \________________________________/\_____________|____|/ \__/       \__/
//   |                    |                          |    |    |    |          |
//scheme              authority                    path   |    |    interpretable as keys
// name   \_______________________________________________|____|/     \____/     \_____/
//   |                         |                          |    |         |           |
//   |                 hierarchical part                  |    |    interpretable as values
//   |                                                    |    |
//   |            path               interpretable as filename |
//   |   ___________|____________                              |
//  / \ /                        \                             |
//  urn:example:animal:ferret:nose               interpretable as extension
//
// HTTP EXAMPLE
//
// scheme
//  name       userinfo       hostname       path        query      anchor
//  _|__   _______|_______   ____|____   _____|_____  _____|______  ___|___
// /    \ /               \ /         \ /           \/            \/       \
// http://username:password@example.com/path/file.ext?subject=Topic#position
//
#include "StdAfx.h"
#include "CrackURL.h"
#include "DefuseBOM.h"
#include "ConvertWideString.h"
#include <winhttp.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

LPCTSTR CrackedURL::m_unsafeString   = " \"<>#{}|\\^~[]`";
LPCTSTR CrackedURL::m_reservedString = "$&/;?@-!*()'"; // ".,+_:="

CrackedURL::CrackedURL()
{
  Reset();
}

// CRACK A URL/URI
CrackedURL::CrackedURL(CString p_url)
{
  CrackURL(p_url);
}

// Explicit DTOR was needed to prevent crashes in production code
// Several crash reports support this sad behavior
CrackedURL::~CrackedURL()
{
  m_parameters.clear();
}

void
CrackedURL::Reset()
{
  // Reset status
  m_valid = false;
  // Reset the answering structure
  m_scheme = "http";
  m_secure = false;
  m_userName.Empty();
  m_password.Empty();
  m_host.Empty();
  m_port = INTERNET_DEFAULT_HTTP_PORT;
  m_path.Empty();
  m_parameters.clear();
  m_anchor.Empty();

  // reset the found-statuses
  m_foundScheme     = false;
  m_foundSecure     = false;
  m_foundUsername   = false;
  m_foundPassword   = false;
  m_foundHost       = false;
  m_foundPort       = false;
  m_foundPath       = false;
  m_foundParameters = false;
  m_foundAnchor     = false;
}

bool
CrackedURL::CrackURL(CString p_url)
{
  // Reset total url
  Reset();

  // Check that there IS an URL!
  if(p_url.IsEmpty())
  {
    return false;
  }
  // Find the scheme
  int pos = p_url.Find(':');
  if(pos <= 0)
  {
    return false;
  }
  m_foundScheme = true;
  m_scheme = p_url.Left(pos);
  if(m_scheme.CompareNoCase("https") == 0)
  {
    m_secure      = true;
    m_foundSecure = true;
    m_port        = INTERNET_DEFAULT_HTTPS_PORT;
  }
  // Remove the scheme
  p_url = p_url.Mid(pos + 1);
  
  // Check for '//'
  if(p_url.GetAt(0) != '/' && p_url.GetAt(1) != '/')
  {
    return false;
  }
  // Remove '//'
  p_url = p_url.Mid(2);
  
  // Check for username/password
  pos = p_url.Find('@');
  if(pos > 0)
  {
    m_foundUsername = true;
    CString authent = p_url.Left(pos);
    p_url = p_url.Mid(pos + 1);
    
    // Find password
    pos = authent.Find(':');
    if(pos < 0)
    {
      m_userName = authent;
    }
    else
    {
      m_foundPassword = true;
      m_userName = authent.Left(pos);
      m_password = authent.Mid(pos + 1);
    }
  }
  
  // Check for server:port
  CString server;
  pos = p_url.Find('/');
  if(pos <= 0)
  {
    // No absolute pathname
    server = p_url;
    p_url.Empty();
  }
  else
  {
    m_foundPath = true;
    server = p_url.Left(pos);
    p_url = p_url.Mid(pos);
  }
  // Find the port. 
  // BEWARE OF IPv6 TEREDO ADDRESSES!!
  // So skip over '[ad:::::a:b:c]' part
  pos = server.Find('[');
  if(pos >= 0)
  {
    pos = server.Find(']',pos+1);
  }
  pos = server.Find(':',pos + 1);
  if(pos > 0)
  {
    m_host = server.Left(pos);
    m_port = atoi(server.Mid(pos + 1));
    m_foundPort = true;
  }
  else
  {
    // No port specified
    m_host = server;
    m_port = m_secure ? INTERNET_DEFAULT_HTTPS_PORT 
                      : INTERNET_DEFAULT_HTTP_PORT;
  }
  
  // Check that there IS a server host
  if(m_host.IsEmpty())
  {
    return false;
  }
  
  // Find Query or Anchor
  int query  = p_url.Find('?');
  int anchor = p_url.Find('#');
  
  if(query > 0 || anchor > 0)
  {
    // Chop of the anchor
    if(anchor > 0)
    {
      m_foundAnchor = true;
      m_anchor = DecodeURLChars(p_url.Mid(anchor + 1));
      p_url    = p_url.Left(anchor);
    }
    // Remember the absolute path name
    if(query > 0)
    {
      m_path = p_url.Left(query);
      p_url  = p_url.Mid(query + 1);
    }
    else
    {
      m_path = p_url;
    }
    if(m_path.GetLength() > 0)
    {
      m_foundPath = true;
      m_path = DecodeURLChars(m_path);
    }

    // Find all query parameters
    while(query > 0)
    {
      // FindNext query
      query = p_url.Find('&');
      CString part;
      if(query > 0)
      {
        part  = p_url.Left(query);
        p_url = p_url.Mid(query + 1);
      }
      else
      {
        part = p_url;
      }

      UriParam param;
      pos = part.Find('=');
      if(pos > 0)
      {
        param.m_key   = DecodeURLChars(part.Left(pos));
        param.m_value = DecodeURLChars(part.Mid(pos + 1),true);
      }
      else
      {
        // No value. Use as key only
        param.m_key = DecodeURLChars(part);
      }
      // Save parameter
      m_parameters.push_back(param);
      m_foundParameters = true;
    }
  }
  else
  {
    // No query and no anchor	
    m_path = p_url;
    m_foundPath = !m_path.IsEmpty();
  }
  // Reduce path: various 'mistakes'
  m_path.Replace("//","/");
  m_path.Replace("\\\\","\\");
  m_path.Replace("\\","/");

  // Find the extension for the media type
  // Media types are stored without the '.'
  pos = m_path.ReverseFind('.');
  if(pos >= 0)
  {
    m_extension = m_path.Mid(pos + 1);
  }

  // Now a valid URL
  return (m_valid = true);
}

// Convert string to URL encoding, using UTF-8 chars
CString
CrackedURL::EncodeURLChars(CString p_text,bool p_queryValue /*=false*/)
{
  CString encoded;

  // Now encode MBCS to UTF-8
  uchar* buffer = nullptr;
  int    length = 0;
  if(TryCreateWideString(p_text,"",false,&buffer,length))
  {
    bool foundBom = false;
    if(TryConvertWideString(buffer,length,"utf-8",encoded,foundBom))
    {
      p_text = encoded;
      encoded.Empty();
    }
  }
  delete [] buffer;

  // Re-encode the string. 
  // Watch out: strange code ahead!
  for(int ind = 0;ind < p_text.GetLength(); ++ind)
  {
    unsigned char ch = p_text.GetAt(ind);
    if(ch == '?')
    {
      p_queryValue = true;
    }
    if(ch == ' ' && p_queryValue)
    {
      encoded += '+';
    }
    else if(strchr(m_unsafeString,ch) ||                      // " \"<>#{}|\\^~[]`"     -> All strings
           (p_queryValue && strchr(m_reservedString,ch)) ||   // "$&+,./:;=?@-!*()'"   -> In query parameter value strings
           (ch < 0x20) ||                                     // 7BITS ASCII Control characters
           (ch > 0x7F) )                                      // Converted UTF-8 characters
    {
      encoded.AppendFormat("%%%2X",ch);
    }
    else
    {
      encoded += ch;
    }
  }
  return encoded;
}

// Decode URL strings, including UTF-8 encoding
CString
CrackedURL::DecodeURLChars(CString p_text,bool p_queryValue /*=false*/)
{
  CString encoded;
  CString decoded;
  bool  convertUTF = false;

  // Whole string decoded for %XX strings
  for(int ind = 0;ind < p_text.GetLength(); ++ind)
  {
    unsigned char ch = GetHexcodedChar(p_text,ind,p_queryValue);
    decoded += ch;
    if(ch > 0x7F)
    {
      convertUTF = true;
    }
  }

  // Only if we found Unicode chars, should we start the whole shebang!
  if(convertUTF)
  {
    // Now decode the UTF-8 in the encoded string, to decoded MBCS
    uchar* buffer = nullptr;
    int    length = 0;
    if(TryCreateWideString(decoded,"utf-8",false,&buffer,length))
    {
      bool foundBom = false;
      if(TryConvertWideString(buffer,length,"",encoded,foundBom))
      {
        decoded = encoded;
      }
    }
    delete [] buffer;
  }
  return decoded;
}

// Decode 1 hex char for URL decoding
unsigned char
CrackedURL::GetHexcodedChar(CString& p_text,int& p_index,bool& p_queryValue)
{
  unsigned char ch = p_text.GetAt(p_index);

  if(ch == '%')
  {
    int num1 = 0;
    int num2 = 0;
    
         if(isdigit (p_text.GetAt(++p_index))) num1 = p_text.GetAt(p_index) - '0';
    else if(isxdigit(p_text.GetAt(  p_index))) num1 = toupper(p_text.GetAt(p_index)) - 'A' + 10;
         if(isdigit (p_text.GetAt(++p_index))) num2 = p_text.GetAt(p_index) - '0';
    else if(isxdigit(p_text.GetAt(  p_index))) num2 = toupper(p_text.GetAt(p_index)) - 'A' + 10;

    ch = (unsigned char)(16 * num1 + num2);
  }
  else if(ch == '?')
  {
    p_queryValue = true;
  }
  else if(ch == '+' && p_queryValue)
  {
    // See RFC 1630: Google Chrome still does this!
    ch = ' ';
  }
  return ch;
}

// Resulting URL
// Reconstruct the URL parts to a complete URL
// foo://username:password@example.com:8042/over/there/index.dtb?type=animal&name=white%20narwhal#nose
CString
CrackedURL::URL()
{
  CString url(m_scheme);

  // Secure HTTP 
  if(m_secure && m_scheme.CompareNoCase("https") != 0)
  {
    url += "s";
  }
  // Separator
  url += "://";

  // Username
  if(!m_userName.IsEmpty())
  {
    url += EncodeURLChars(m_userName);

    if(!m_password.IsEmpty())
    {
      url += ":";
      url += EncodeURLChars(m_password);
    }
    // Separating the user
    url += "@";
  }
 
  // Add hostname
  url += m_host;

  // Possibly add a port
  if( (m_secure == false && m_port != INTERNET_DEFAULT_HTTP_PORT) ||
      (m_secure == true  && m_port != INTERNET_DEFAULT_HTTPS_PORT) )
  {
    CString portnum;
    portnum.Format(":%d",m_port);
    url += portnum;
  }

  // Now add absolute path and possibly parameters and anchor
  url += AbsolutePath();

  // This is the result
  return url;
}

// Safe URL (without the userinfo)
CString
CrackedURL::SafeURL()
{
  CString url(m_scheme);

  // Secure HTTP 
  if(m_secure && m_scheme.CompareNoCase("https") != 0)
  {
    url += "s";
  }
  // Separator
  url += "://";

  // Add hostname
  url += m_host;

  // Possibly add a port
  if((m_secure == false && m_port != INTERNET_DEFAULT_HTTP_PORT) ||
     (m_secure == true  && m_port != INTERNET_DEFAULT_HTTPS_PORT))
  {
    CString portnum;
    portnum.Format(":%d",m_port);
    url += portnum;
  }

  // Now add absolute path and possibly parameters and anchor
  url += AbsolutePath();

  // This is the result
  return url;
}

// Resulting absolute path
CString 
CrackedURL::AbsoluteResource()
{
  // Begin with the base path
  return EncodeURLChars(m_path);
}

// Resulting absolute path + parameters + anchor
// But without protocol and user/password/server/port
CString
CrackedURL::AbsolutePath()
{
  // Begin with the base path
  CString url = EncodeURLChars(m_path);

  // Adding parameters?
  if(m_parameters.size())
  {
    url += "?";

    UriParams::iterator it;
    for(it = m_parameters.begin();it != m_parameters.end(); ++it)
    {
      UriParam& param = *it;

      // Next parameter
      if(it != m_parameters.begin())
      {
        url += "&";
      }
      url += EncodeURLChars(param.m_key);
      if(!param.m_value.IsEmpty())
      {
        url += "=";
        url += EncodeURLChars(param.m_value,true);
      }
    }
  }

  // Adding the anchor
  if(!m_anchor.IsEmpty())
  {
    url += "#";
    url += EncodeURLChars(m_anchor);
  }

  // This is the result
  return url;
}

// As a resulting UNC path for a file
// \\server@port\abs-path\file.ext
CString
CrackedURL::UNC()
{
  CString path("\\\\");

  path += m_host;

  if(m_port != INTERNET_DEFAULT_HTTP_PORT)
  {
    path += "@";
    path.AppendFormat("%d",m_port);
  }
  
  if(!m_path.IsEmpty())
  {
    CString pathname(m_path);
    pathname.Replace("/","\\");

    path += "\\";//??
    path += pathname;
  }
  return path;
}

CrackedURL* 
CrackedURL::operator=(CrackedURL* p_orig)
{
  // Copy the info
  m_valid    = p_orig->m_valid;
  m_scheme   = p_orig->m_scheme;
  m_secure   = p_orig->m_secure;
  m_userName = p_orig->m_userName;
  m_password = p_orig->m_password;
  m_host     = p_orig->m_host;
  m_port     = p_orig->m_port;
  m_path     = p_orig->m_path;
  m_anchor   = p_orig->m_anchor;

  // Copy status info
  m_foundScheme     = p_orig->m_foundScheme;
  m_foundSecure     = p_orig->m_foundSecure;
  m_foundUsername   = p_orig->m_foundUsername;
  m_foundPassword   = p_orig->m_foundPassword;
  m_foundHost       = p_orig->m_foundHost;
  m_foundPort       = p_orig->m_foundPort;
  m_foundPath       = p_orig->m_foundPath;
  m_foundParameters = p_orig->m_foundParameters;
  m_foundAnchor     = p_orig->m_foundAnchor;

  // Copy all parameters
  UriParams::iterator it;
  for(it  = p_orig->m_parameters.begin();
      it != p_orig->m_parameters.end(); 
      ++it)
  {
     UriParam param;
     param.m_key   = it->m_key;
     param.m_value = it->m_value;
     m_parameters.push_back(param);
  }
  
  // Return ourselves
  return this;
}

// Has parameter value
bool
CrackedURL::HasParameter(CString p_parameter)
{
  for(unsigned i = 0; i < m_parameters.size(); ++i)
  {
    if(m_parameters[i].m_key.CompareNoCase(p_parameter) == 0)
    {
      return true;
    }
  }
  return false;
}

CString 
CrackedURL::GetParameter(CString p_parameter)
{
  for(auto& param : m_parameters)
  {
    if(param.m_key.CompareNoCase(p_parameter) == 0)
    {
      return param.m_value;
    }
  }
  return "";
}

void    
CrackedURL::SetParameter(CString p_parameter,CString p_value)
{
  // See if we already have this parameter name
  for(auto& param : m_parameters)
  {
    if(param.m_key.CompareNoCase(p_parameter) == 0)
    {
      param.m_value = p_value;
      return;
    }
  }

  // Make a new parameter for the URL
  UriParam param;
  param.m_key   = p_parameter;
  param.m_value = p_value;

  // Save them
  m_parameters.push_back(param);
  m_foundParameters = true;
}

UriParam*
CrackedURL::GetParameter(unsigned p_parameter)
{
  if(p_parameter < (unsigned)m_parameters.size())
  {
    return &m_parameters[p_parameter];
  }
  return nullptr;
}
