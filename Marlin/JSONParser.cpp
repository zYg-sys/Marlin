/////////////////////////////////////////////////////////////////////////////////
//
// SourceFile: JSONParser.cpp
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
#include "stdafx.h"
#include "bcd.h"
#include "JSONParser.h"
#include "XMLParser.h"
#include "DefuseBOM.h"
#include "ConvertWideString.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

JSONParser::JSONParser(JSONMessage* p_message)
           :m_message(p_message)
{
}

void
JSONParser::SetError(JsonError p_error,const char* p_text,bool p_throw /*= true*/)
{
  if(m_message)
  {
    m_message->m_errorstate = true;
    m_message->m_lastError.Format("ERROR [%d] on line [%d] ",p_error,m_lines);
    m_message->m_lastError += p_text;
  }
  if(p_throw)
  {
    throw p_error;
  }
}

void
JSONParser::ParseMessage(CString& p_message,bool& p_whitespace)
{
  // Check if we have something to do
  if(m_message == nullptr)
  {
    SetError(JsonError::JE_Empty,"Empty message");
    return;
  }

  // Initializing the parser
  m_pointer    = (uchar*) p_message.GetString();
  m_valPointer = m_message->m_value;
  m_lines      = 1;
  m_objects    = 0;

  // Check for Byte-Order-Mark first
  BOMType bomType = BOMType::BT_NO_BOM;
  unsigned int skip = 0;
  BOMOpenResult bomResult = CheckForBOM(m_pointer,bomType,skip);

  if(bomResult != BOMOpenResult::BOR_NoBom)
  {
    if(bomType != BOMType::BT_BE_UTF8)
    {
      // cannot process these strings
      SetError(JsonError::JE_IncompatibleEncoding,"Incompatible Byte-Order-Mark encoding");
      return;
    }
    // Skip past BOM
    m_pointer += skip;
  }

  // See if we have an empty message string
  SkipWhitespace();
  if(p_message.IsEmpty())
  {
    // Regular empty message.
    // Nothing here but whitespace
    return;
  }

  // MAIN PARSING LOOP
  try
  {
    // Parse a JSON level
    ParseLevel();

    if(m_pointer && *m_pointer)
    {
      SetError(JsonError::JE_ExtraText,(const char*)m_pointer);
    }
  }
  catch(JsonError& /*error*/)
  {
    // Error message text already set
  }
  
  // Preserving whitespace setting
  p_whitespace = (m_lines > m_objects);
}

void
JSONParser::SkipWhitespace()
{
  while(isspace(*m_pointer))
  {
    if(*m_pointer == '\n')
    {
      ++m_lines;
    }
    ++m_pointer;
  }
}

// Recursive descent parser for JSON
void
JSONParser::ParseLevel()
{
  SkipWhitespace();

  if(!ParseString())
  {
    if(!ParseNumber())
    {
      if(!ParseArray())
      {
        if(!ParseObject())
        {
          if(!ParseConstant())
          {
            if(m_pointer && *m_pointer)
            {
              SetError(JsonError::JE_UnknownString,"Non conforming JSON message text");
            }
          }
        }
      }
    }
  }
  SkipWhitespace();
}

// Parse constants
bool
JSONParser::ParseConstant()
{
  if(_strnicmp((const char*)m_pointer,"null",4) == 0)
  {
    m_valPointer->SetValue(JsonConst::JSON_NULL);
    m_pointer += 4;
  }
  else if(_strnicmp((const char*)m_pointer,"true",4) == 0)
  {
    m_valPointer->SetValue(JsonConst::JSON_TRUE);
    m_pointer += 4;
  }
  else if(_strnicmp((const char*)m_pointer,"false",5) == 0)
  {
    m_valPointer->SetValue(JsonConst::JSON_FALSE);
    m_pointer += 5;
  }
  else
  {
    return false;
  }
  return true;
}

// Gets me a string
CString
JSONParser::GetString()
{
  CString result;
  bool    doUTF8 = false;
  bool    doMBCS = false;

  // Check that we have a string now
  if(*m_pointer != '\"')
  {
    SetError(JsonError::JE_NoString,"String expected but not found!");
  }
  uchar* startPointer(++m_pointer);

  while(*m_pointer && *m_pointer != '\"')
  {
    // See if we must do an escape
    if(*m_pointer == '\\')
    {
      ++m_pointer;
      uchar ch = *m_pointer++;
      switch(ch)
      {
        case '\"': [[fallthrough]];
        case '\\': [[fallthrough]];
        case '/':  result += (char)ch; break;
        case 'b':  result += '\b'; break;
        case 'f':  result += '\f'; break;
        case 'n':  result += '\n'; break;
        case 'r':  result += '\r'; break;
        case 't':  result += '\t'; break;
        case 'u':  m_pointer = startPointer;
                   return GetUnicodeString();
        default:   SetError(JsonError::JE_IllString,"Ill formed string. Illegal escape sequence.");
                   break;
      }
    }
    else
    {
      uchar ch = ValueChar();
      if((ch & 0xC0) == 0xC0 || 
         (ch & 0xE0) == 0xE0 || 
         (ch & 0xF0) == 0xF0  )
      {
        // UTF-8 found. Cannot mix with MBCS
        if(doMBCS)
        {
          m_pointer = startPointer;
          return GetUnicodeString();
        }
        doUTF8 = true;
      }
      else if((ch & 0xC0) != 0x1000)
      {
        // MBCS found. Cannot mix with UTF8
        if(doUTF8)
        {
          m_pointer = startPointer;
          return GetUnicodeString();
        }
        doMBCS = true;
      }
      result += ch;
    }
  }
  // Skip past string's ending
  if(*m_pointer && *m_pointer == '\"')
  {
    ++m_pointer;
  }
  else
  {
    SetError(JsonError::JE_StringEnding,"String found without an ending quote!");
  }

  // Eventually decode UTF-8 encodings
  if(doUTF8)
  {
    result = DecodeStringFromTheWire(result);
  }
  return result;
}

// Conversion of xdigit to a numeric value
uchar
JSONParser::XDigitToValue(int ch)
{
  if(ch >= '0' && ch <= '9')
  {
    return (uchar) (ch - '0');
  }
  if(ch >= 'A' && ch <= 'F')
  {
    return (uchar) (ch - 'A' + 10);
  }
  if(ch >= 'a' && ch <= 'f')
  {
    return (uchar) (ch - 'a' + 10);
  }
  return 0;
}

// Get a character from message
uchar
JSONParser::ValueChar()
{
  if (*m_pointer == '\n')
  {
    ++m_lines;
  }
  return *m_pointer++;
}

// Get an UTF-16 \uXXXX escape char
unsigned short
JSONParser::UnicodeChar()
{
  int  ind = 4;
  unsigned short ch = 0;
  while(*m_pointer && isxdigit(*m_pointer) && ind > 0)
  {
    ch *= 16; // Always radix 16 for JSON
    ch += XDigitToValue(*m_pointer);
    // Next char
    --ind;
    ++m_pointer;
  }
  if(ind)
  {
    SetError(JsonError::JE_Unicode4Chars, "Unicode escape consists of 4 hex characters");
  }
  return ch;
}

// Get a character from message including UTF-8 translation
// Only works on the first 16 bits Multilangual Unicode Plane !!
unsigned short
JSONParser::UTF8Char()
{
  if(*m_pointer == '\n')
  {
    ++m_lines;
  }
  unsigned num = 0;
  unsigned short cp = 0;
  unsigned short bytes = *m_pointer++;

  if((bytes & 0x80) == 0x00)
  {
    // U+0000 to U+007F (Standard 7bit ASCII)
    cp = (bytes & 0x7F);
    num = 1;
  }
  else if((bytes & 0xE0) == 0xC0)
  {
    // U+0080 to U+07FF (one extra char. cp = first 9 bits)
    cp = (bytes & 0x1F);
    num = 2;
  }
  else if((bytes & 0xF0) == 0xE0)
  {
    // U+0800 to U+FFFF (two extra char. cp = first 8 bits)
    cp = (bytes & 0x0F);
    num = 3;
  }
  else if((bytes & 0xF8) == 0xF0)
  {
    // U+10000 to U+10FFFF (three extra char. cp = first 3 bits)
    cp = (bytes & 0x07);
    num = 4;
  }
  else
  {
    // Convert MBCS to 16 bit UTF16-Codepoint
    CString string;
    string += ((unsigned char)bytes);
    uchar* buffer = nullptr;
    int    length = 0;
    if(TryCreateWideString(string,"",false,&buffer,length))
    {
      cp = *((unsigned short*)buffer);
    }
    delete[] buffer;
    if(cp)
    {
      return cp;
    }
    SetError(JsonError::JE_IllString,"Ill formed string. Illegal UTF-8 characters.",false);
    return '?';
  }
  for(unsigned i = 1; i < num; ++i)
  {
    bytes = *m_pointer++;
    if((bytes & 0xC0) != 0x80)
    {
      // Not a UTF-8 6 bit follow up char
      SetError(JsonError::JE_IllString,"Ill formed string. Illegal UTF-8 follow-up characters.",false);
      return '?';
    }
    cp = (cp << 6) | (bytes & 0x3F);
  }
  return cp;
}

// The current string contains an \u escape sequence
// Restart the scanning of the string on the basis of UTF-16 chars in the string
CString 
JSONParser::GetUnicodeString()
{
  uchar* start(m_pointer);

  // Find the length of the string
  int length = 0;
  while(*m_pointer && (*m_pointer++ != '\"')) ++length;
  if(!*m_pointer)
  {
    SetError(JsonError::JE_StringEnding,"UTF-16 String found without an ending quote!");
    return "";
  }
  m_pointer = start;

  // Allocate a array of shorts (2 bytes) for the string
  unsigned short* result = new unsigned short[length + 1];
  memset(result,0,2 * (length + 1));

  // Fill the array with chars and unicode chars
  unsigned short* resPointer = result;
  while(*m_pointer && *m_pointer != '\"')
  {
    // See if we must do an escape
    if(*m_pointer == '\\')
    {
      ++m_pointer;
      unsigned short ch = *m_pointer++;
      switch(ch)
      {
        case '\"': [[fallthrough]];
        case '\\': [[fallthrough]];
        case '/':  *resPointer = ch;    break;
        case 'b':  *resPointer = '\b';  break;
        case 'f':  *resPointer = '\f';  break;
        case 'n':  *resPointer = '\n';  break;
        case 'r':  *resPointer = '\r';  break;
        case 't':  *resPointer = '\t';  break;
        case 'u':  *resPointer = UnicodeChar(); break;
        default:   SetError(JsonError::JE_IllString,"Ill formed string. Illegal escape sequence.");
                   break;
      }
      ++resPointer;
    }
    else
    {
      unsigned short ch = UTF8Char();
      if(ch)
      {
        *resPointer++ = ch;
      }
    }
  }

  // Convert the UTF-16 string back to MBCS
  CString decoded;
  bool foundBom = false;

  if(!TryConvertWideString((const uchar*)result,length,"",decoded,foundBom))
  {
    decoded.Empty();
    SetError(JsonError::JE_IllString,"Ill formed string. Could not convert UTF-16 characters.");
  }
  delete[] result;
  return decoded;
}

bool
JSONParser::ParseString()
{
  if(*m_pointer != '\"')
  {
    return false;
  }
  CString result = GetString();

  // Found our string
  m_valPointer->SetValue(result);
  return true;
}

// Try to parse an array
bool
JSONParser::ParseArray()
{
  // Must we do an array?
  if(*m_pointer != '[')
  {
    return false;
  }
  ++m_objects;
  // Go to begin and set value to array
  ++m_pointer;
  SkipWhitespace();
  m_valPointer->SetDatatype(JsonType::JDT_array);

  // Loop through all array values
  int elements = 0;
  while(*m_pointer)
  {
    // Check for an empty array
    if(*m_pointer == ']' && elements == 0)
    {
      ++m_pointer;
      return true;
    }
    ++elements;

    // Array is not empty
    // Put array element extra in the array
    JSONvalue value;
    m_valPointer->GetArray().push_back(value);

    // Put value pointer on the stack and parse an array value
    JSONvalue* workPointer = m_valPointer;
    m_valPointer = &m_valPointer->GetArray().back();
    ParseLevel();
    m_valPointer = workPointer;

    // End of the array found?
    if(*m_pointer == ']')
    {
      ++m_pointer;
      break;
    }
    // Must now find ',' for next array value
    if(*m_pointer != ',')
    {
      SetError(JsonError::JE_ArrayElement,"Array element separator ',' expected!");
    }
    // Skip past array separator
    ++m_pointer;
  }
  return true;
}

// Try to parse an object
bool
JSONParser::ParseObject()
{
  // Must we do an object?
  if(*m_pointer != '{')
  {
    return false;
  }
  ++m_objects;
  // Go to begin and set value in the object
  ++m_pointer;
  SkipWhitespace();
  m_valPointer->SetDatatype(JsonType::JDT_object);

  // Loop through all object values
  int elements = 0;
  while(*m_pointer)
  {
    JSONpair value;
    m_valPointer->GetObject().push_back(value);
    JSONpair& pair = m_valPointer->GetObject().back();

    // Check for an empty object
    if(*m_pointer == '}' && elements == 0)
    {
      ++m_pointer;
      return true;
    }
    ++elements;

    // Parse the name string;
    CString name = GetString();
    pair.m_name = name;

    SkipWhitespace();
    if(*m_pointer != ':')
    {
      SetError(JsonError::JE_ObjNameSep,"Object's name-value separator ':' is missing!");
    }
    ++m_pointer;
    SkipWhitespace();

    // Put pointer on the stack and parse a value
    JSONvalue* workPointer = m_valPointer;
    m_valPointer = &pair.m_value;
    ParseLevel();
    m_valPointer = workPointer;

    // End of the object found?
    if(*m_pointer == '}')
    {
      ++m_pointer;
      break;
    }
    // Must now find ',' for next object value
    if(*m_pointer != ',')
    {
      SetError(JsonError::JE_ObjectElement,"Object element separator ',' expected!");
    }
    // Skip past object separator
    ++m_pointer;
    SkipWhitespace();
  }
  return true;
}

// Parse a number
bool
JSONParser::ParseNumber()
{
  // See if we must parse a number
  if(*m_pointer != '-' && !isdigit(*m_pointer))
  {
    return false;
  }

  // Locals
  bcd     bcdNumber;
  int     factor    = 1;
  __int64 number    = 0;
  int     intNumber = 0;
  JsonType type = JsonType::JDT_number_int;

  // See if we find a negative number
  if(*m_pointer == '-')
  {
    factor = -1;
    ++m_pointer;
  }

  // Finding the integer part
  while(*m_pointer && isdigit(*m_pointer))
  {
    number *= 10; // JSON is always in radix 10!
    number += (*m_pointer - '0');
    ++m_pointer;
  }

  // Finding a broken number
  if(*m_pointer == '.' || tolower(*m_pointer) == 'e')
  {
    // Prepare
    if(*m_pointer == '.')
    {
      ++m_pointer;
      type = JsonType::JDT_number_bcd;
      bcdNumber = number;
      bcd decimPart = 1;

      while(*m_pointer && isdigit(*m_pointer))
      {
        decimPart *= 10;
        bcdNumber += ((bcd)(*m_pointer - '0')) / decimPart;
        ++m_pointer;
      }
    }
    // Do the exponential?
    if(tolower(*m_pointer) == 'e')
    {
      // Prepare
      ++m_pointer;
      int exp = 0;
      int fac = 1;

      // Negative exponential?
      if(*m_pointer == '-')
      {
        fac = -1;
        ++m_pointer;
      }

      // Find all exponential digits
      while(*m_pointer && isdigit(*m_pointer))
      {
        exp *= 10;
        exp += (*m_pointer - '0');
        ++m_pointer;
      }
      bcdNumber *= pow(10.0,exp * fac);
    }

    // Do not forget the sign
    bcdNumber *= factor;
  }
  else
  {
    number *= factor;
    if((number > MAXINT32) || (number < MININT32))
    {
      type = JsonType::JDT_number_bcd;
      bcdNumber = number;
    }
    else
    {
      // Static cast allowed because test on MAXINT32/MININT32
      intNumber = static_cast<int>(number);
    }
  }

  // Preserving our findings
  if(type == JsonType::JDT_number_int)
  {
    m_valPointer->SetValue(intNumber);
  }
  else // if(type == JDT_number_dbl)
  {
    m_valPointer->SetValue(bcdNumber);
  }
  return true;
}

//////////////////////////////////////////////////////////////////////////
//
// PARSER SOAPMessage -> JSONMessage
//
//////////////////////////////////////////////////////////////////////////

#define WHITESPACE "\r\n\t\f "

JSONParserSOAP::JSONParserSOAP(JSONMessage* p_message)
               :JSONParser(p_message)
{
}

JSONParserSOAP::JSONParserSOAP(JSONMessage* p_message,SOAPMessage* p_soap)
               :JSONParser(p_message)
{
  // Construct the correct contents!!
  p_soap->CompleteTheMessage();

  JSONvalue&  valPointer = *m_message->m_value;
  XMLElement& element    = *p_soap->GetParameterObjectNode();

  ParseMain(valPointer,element);
}

JSONParserSOAP::JSONParserSOAP(JSONMessage* p_message,XMLMessage* p_xml)
               :JSONParser(p_message)
{
  JSONvalue&  valPointer = *m_message->m_value;
  XMLElement& element    = *p_xml->GetRoot();

  ParseMain(valPointer,element);
}

void
JSONParserSOAP::Parse(XMLElement* p_element,bool p_forceerArray /*=false*/)
{
  m_forceerArray = p_forceerArray;
  ParseMain(*m_message->m_value,*p_element);
}

void
JSONParserSOAP::ParseMain(JSONvalue& p_valPointer,XMLElement& p_element)
{
  p_valPointer.SetDatatype(JsonType::JDT_object);
  JSONobject object;
  JSONpair pair;
  pair.m_name = p_element.GetName();
  pair.m_value.SetValue(JsonConst::JSON_NULL);
  object.push_back(pair);
  p_valPointer.SetValue(object);

  JSONpair& npair = p_valPointer.GetObject().back();
  JSONvalue& value = npair.m_value;

  if(!p_element.GetChildren().empty())
  {
    value.SetDatatype(JsonType::JDT_object);
    ParseLevel(value,p_element);
  }
}

void
JSONParserSOAP::ParseLevel(JSONvalue& p_valPointer,XMLElement& p_element)
{
  if(p_element.GetChildren().size())
  {
    CString arrayName;
    bool makeArray = ScanForArray(p_element,arrayName);

    if(makeArray)
    {
      CreateArray(p_valPointer,p_element,arrayName);
    }
    else
    {
      CreateObject(p_valPointer,p_element);
    }
  }
  else
  {
    // Create a pair
    CreatePair(p_valPointer,p_element);
  }
}

bool 
JSONParserSOAP::ScanForArray(XMLElement& p_element,CString& p_arrayName)
{
  // If the WSDL type tells us that there is an array af same elements
  // Under this element, we will convert to an array in JSON
  if(p_element.GetType() & XDT_Array)
  {
    return true;
  }

  // See if we must scan
  if(p_element.GetChildren().empty())
  {
    return false;
  }

  // Special case 1 element, but we must do an array
  if(p_element.GetChildren().size() == 1)
  {
    if(m_forceerArray)
    {
      p_arrayName = p_element.GetChildren().front()->GetName();
      return true;
    }
    return false;
  }

  CString sameName;
  bool first = true;

  // Scan the underlying elements
  for(auto& element : p_element.GetChildren())
  {
    if(first)
    {
      first = false;
      sameName = element->GetName();
    }
    else if(sameName.Compare(element->GetName()))
    {
      return false;
    }
  }

  // Yip, it's an array
  p_arrayName = sameName;
  return true;
}

// Scan for an array halfway into an object vector
// 0 -> No extra array needed
// 1 -> Already a name found
// 2 -> Create a new array
int
JSONParserSOAP::ScanForArray(XMLElement& p_element)
{
  XMLElement* parent = p_element.GetParent();
  XMLElement* before = nullptr;
  XMLElement* after  = nullptr;

  XmlElementMap::iterator it = parent->GetChildren().begin();
  while(it != parent->GetChildren().end())
  {
    if(*it == &p_element)
    {
      break;
    }
    before = *it++;
    if(m_forceerArray && p_element.GetName().Compare(before->GetName()) == 0)
    {
      return 1;
    }
  }
  if(*it == &p_element && ++it != parent->GetChildren().end())
  {
    after = *(it);
  }

  if(before && p_element.GetName().Compare(before->GetName()) == 0)
  {
    return 1;
  }
  if(after && p_element.GetName().Compare(after->GetName()) == 0)
  {
    return 2;
  }

  if(m_forceerArray)
  {
    // There is a same node further on
    while(it != parent->GetChildren().end())
    {
      if(p_element.GetName().Compare((*(it))->GetName()) == 0)
      {
        return 2;
      }
      ++it;
    }
  }
  return 0;
}

void
JSONParserSOAP::CreateArray(JSONvalue& p_valPointer,XMLElement& p_element,CString p_arrayName)
{
  JSONarray  jarray;
  JSONpair   pair;
  pair.m_name = p_arrayName;
  pair.m_value.SetValue(jarray);

  if(p_valPointer.GetDataType() != JsonType::JDT_object)
  {
    p_valPointer.SetDatatype(JsonType::JDT_object);
  }
  p_valPointer.GetObject().push_back(pair);

  JSONpair& jpair = p_valPointer.GetObject().back();
  JSONarray& jar = jpair.m_value.GetArray();

  for(auto& element : p_element.GetChildren())
  {
    JSONvalue* append = nullptr;
    CString text = element->GetValue();
    Trim(text);

    JSONobject objVal;

    if(element->GetAttributes().size() > 0)
    {
      for(auto& attribute : element->GetAttributes())
      {
        JSONpair attrPair;
        attrPair.m_name = attribute.m_name;
        attrPair.m_value.SetValue(attribute.m_value);

        // Put an object in the array and parse it
        objVal.push_back(attrPair);
      }
      if(!text.IsEmpty())
      {
        JSONpair textPair;
        textPair.m_name = "text";
        textPair.m_value.SetValue(text);
        objVal.push_back(textPair);
      }

      // Put in the array
      JSONvalue value;
      jar.push_back(value);
      JSONvalue& val = jar.back();
      val.SetValue(objVal);
      append = &val;
    }
    else if(!text.IsEmpty())
    {
      JSONvalue val(text);
      jar.push_back(val);
      append = &(jar.back());
    }

    // Parse on, if something to do
    if(!element->GetChildren().empty())
    {
      if(append == nullptr || append->GetDataType() != JsonType::JDT_object)
      {
        JSONvalue val(JsonType::JDT_object);
        jar.push_back(val);
        append = &(jar.back());
      }
      ParseLevel(*append,*element);
    }
  }
}

void 
JSONParserSOAP::CreateObject(JSONvalue& p_valPointer,XMLElement& p_element)
{
  // Adding to an array. First add an object
  if(p_valPointer.GetDataType() == JsonType::JDT_array)
  {
    JSONvalue val(JsonType::JDT_object);
    p_valPointer.Add(val);
  }

  for(auto& element : p_element.GetChildren())
  {
    CString value;
    JSONvalue*  here   = nullptr;
    JSONobject* objPtr = nullptr;
    JSONvalue* valPointer = &p_valPointer;

    if(p_valPointer.GetDataType() == JsonType::JDT_array)
    {
      valPointer = &(p_valPointer.GetArray().back());
    }

    int makeArray = ScanForArray(*element);
    if(makeArray == 1)
    {
      JsonType searchArray(JsonType::JDT_array);
      JSONvalue* val = m_message->FindValue(&p_valPointer,element->GetName(),false,false,&searchArray);
      JSONarray* arr = &(val->GetArray());
      JSONvalue object(JsonType::JDT_object);
      arr->push_back(object);
      valPointer = here = &(arr->back());
      value = element->GetValue();
      Trim(value);
    }
    else
    {
      CreatePair(*valPointer,*element);
      if(valPointer->GetDataType() == JsonType::JDT_object &&
        !valPointer->GetObject().empty())
      {
        here = &(valPointer->GetObject().back().m_value);
        if(!element->GetAttributes().empty())
        {
          value = element->GetValue();
          Trim(value);
        }
      }
    }
    if(makeArray == 2)
    {
      // Change the just added pair to an array
      if(here->GetDataType() == JsonType::JDT_array && !here->GetArray().empty())
      {
        value = here->GetArray().back().GetString();
      }
      here->SetDatatype(JsonType::JDT_array);
      JSONvalue val(JsonType::JDT_object);
      here->Add(val);
      here = &(here->GetArray().back());
    }

    // Preserve the innertext of the element
    if(value.IsEmpty() && here)
    {
      value = here->GetString();
    }

    // Do the attributes
    if(element->GetAttributes().size() > 0)
    {
      JSONobject obj;
      here->SetValue(obj);
      objPtr = &here->GetObject();
        
      JSONpair attrPair;
      for(auto& attribute : element->GetAttributes())
      {
        attrPair.m_name = attribute.m_name;
        attrPair.m_value.SetValue(attribute.m_value);
        objPtr->push_back(attrPair);
      }

      // Append the text node that was overwritten by the attributes
      if(!value.IsEmpty())
      {
        JSONpair textPair;
        textPair.m_name = "text";
        textPair.m_value.SetValue(value);
        objPtr->push_back(textPair);
      }
    }

    // Do the children
    if(element->GetChildren().size())
    {
      if(here->GetDataType() == JsonType::JDT_const)
      {
        here->SetDatatype(JsonType::JDT_object);
      }
      ParseLevel(*here,*element);
    }
  }
}

void 
JSONParserSOAP::CreatePair(JSONvalue& p_valPointer,XMLElement& p_element)
{
  if(p_valPointer.GetDataType() == JsonType::JDT_object)
  {
    JSONpair pair;
    pair.m_name = p_element.GetName();
    CString value = p_element.GetValue();
    Trim(value);

    if(m_forceerArray)
    {
      pair.m_value.SetDatatype(JsonType::JDT_array);
      if(p_element.GetChildren().empty())
      {
        JSONvalue val(JsonConst::JSON_NULL);
        if(!value.IsEmpty())
        {
          val.SetValue(value);
        }
        pair.m_value.Add(val);
      }
    }
    else
    {
      pair.m_value.SetValue(JsonConst::JSON_NULL);
      if(!value.IsEmpty())
      {
        pair.m_value.SetValue(value);
      }
    }
    // Put in the surrounding object
    p_valPointer.GetObject().push_back(pair);
  }
  else
  {
    // Simple string in an array (SPACES ALLOWED)
    p_valPointer.SetDatatype(JsonType::JDT_string);
    CString value = p_element.GetValue();
    p_valPointer.SetValue(value);
  }
}

void
JSONParserSOAP::Trim(CString& p_value)
{
  while(p_value.GetLength())
  {
    int index = p_value.GetLength() - 1;
    int ch = p_value.GetAt(index);
    if(strchr(WHITESPACE,ch))
    {
      p_value = p_value.Left(index);
    }
    else
    {
      break;
    }
  }

  while(p_value.GetLength())
  {
    if(strchr(WHITESPACE,p_value.GetAt(0)))
    {
      p_value = p_value.Mid(1);
    }
    else break;
  }
}

