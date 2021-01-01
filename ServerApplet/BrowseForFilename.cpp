/////////////////////////////////////////////////////////////////////////////////
//
// SourceFile: BrowseForFilename.cpp
//
// Marlin Component: Internet server/client
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
#include "BrowseForFilename.h"
#include <dlgs.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma warning (disable:4312)

BrowseForFilename::BrowseForFilename(bool    p_open        // true = open, false = SaveAs
                                    ,CString p_title       // Title of the dialog
                                    ,CString p_defext      // Default extension
                                    ,CString p_filename    // Default first file
                                    ,int     p_flags       // Default flags
                                    ,CString p_filter      // Filter for extensions
                                    ,CString p_direct)     
                  :m_open(p_open)
                  ,m_initalDir(p_direct)
{
  if(p_filter.IsEmpty())
  {
    p_filter = "Text files (*.txt)|*.txt|";
  }
  // Register original CWD (Current Working Directory)
  GetCurrentDirectory(MAX_PATH, m_original);
  if(!p_direct.IsEmpty())
  {
    // Change to starting directory // VISTA
    SetCurrentDirectory(p_direct.GetString());
  }
  strncpy_s(m_filter,  1024,    p_filter,  1024);
  strncpy_s(m_filename,MAX_PATH,p_filename,MAX_PATH);
  strncpy_s(m_defext,  100,     p_defext,  100);
  strncpy_s(m_title,   100,     p_title,   100);
  FilterString(m_filter);

  // Fill in the filename structure
  p_flags |= OFN_ENABLESIZING | OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
  p_flags &= ~(OFN_NODEREFERENCELINKS | OFN_NOTESTFILECREATE);

  m_ofn.lStructSize       = sizeof(OPENFILENAME);
  m_ofn.hwndOwner         = AfxGetApp()->GetMainWnd()->GetSafeHwnd();
  m_ofn.hInstance         = (HINSTANCE) GetWindowLongPtr(m_ofn.hwndOwner,GWLP_HINSTANCE);
  m_ofn.lpstrFile         = (LPSTR) m_filename;
  m_ofn.lpstrDefExt       = (LPSTR) m_defext;
  m_ofn.lpstrTitle        = (LPSTR) m_title;
  m_ofn.lpstrFilter       = (LPSTR) m_filter;
  m_ofn.Flags             = p_flags;
  m_ofn.nFilterIndex      = 1;    // Use lpstrFilter
  m_ofn.nMaxFile          = MAX_PATH;
  m_ofn.lpstrCustomFilter = NULL; //(LPSTR) buf_filter;
  m_ofn.nMaxCustFilter    = 0;
  m_ofn.lpstrFileTitle    = NULL;
  m_ofn.nMaxFileTitle     = 0;
  m_ofn.lpstrInitialDir   = (LPCSTR) m_initalDir;
  m_ofn.nFileOffset       = 0;
  m_ofn.lCustData         = NULL;
  m_ofn.lpfnHook          = NULL;
  m_ofn.lpTemplateName    = NULL;
}

BrowseForFilename::~BrowseForFilename()
{
  // Go back to the original directory
  SetCurrentDirectory(m_original);
}

#pragma warning(disable:4702)
int 
BrowseForFilename::DoModal()
{
  int res = IDCANCEL;
  try
  {
    if(m_open)
    {
      res = GetOpenFileName(&m_ofn);
    }
    else
    {
      res = GetSaveFileName(&m_ofn);
    }
  }
  catch(CException& er)
  {
    ::MessageBox(NULL,"Cannot make a filename browsing dialog","ERROR",MB_OK|MB_ICONERROR);
    UNREFERENCED_PARAMETER(er);
  }
  return res;
}

CString 
BrowseForFilename::GetChosenFile()
{
  return (CString) m_ofn.lpstrFile;
}

void
BrowseForFilename::FilterString(char *filter)
{
  char *pnt = filter;
  while(*pnt)
  {
    if(*pnt == '|')
    {
      *pnt = 0;
    }
    ++pnt;
  }
  *pnt++ = 0;
  *pnt++ = 0;
}
