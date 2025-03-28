/*
 * 				Shell Library Functions
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 2000 Juergen Schmied
 * Copyright 2002 Eric Pouech
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <ctype.h>

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "wownt32.h"
#include "shellapi.h"
#include "winuser.h"
#include "wingdi.h"
#include "shlobj.h"
#include "shlwapi.h"

#include "wine/winbase16.h"
#include "wine/winuser16.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

extern HINSTANCE WINAPI WOWShellExecute(HWND hWnd, LPCSTR lpOperation,LPCSTR lpFile,
                                        LPCSTR lpParameters,LPCSTR lpDirectory,
                                        INT iShowCmd, void *callback);

#define HINSTANCE_32(h16)	((HINSTANCE)(ULONG_PTR)(h16))
#define HINSTANCE_16(h32)	(LOWORD(h32))

typedef struct {     /* structure for dropped files */
 WORD     wSize;
 POINT16  ptMousePos;
 BOOL16   fInNonClientArea;
 /* memory block with filenames follows */
} DROPFILESTRUCT16, *LPDROPFILESTRUCT16;

static const char lpstrMsgWndCreated[] = "OTHERWINDOWCREATED";
static const char lpstrMsgWndDestroyed[] = "OTHERWINDOWDESTROYED";
static const char lpstrMsgShellActivate[] = "ACTIVATESHELLWINDOW";

static HWND	SHELL_hWnd = 0;
static HHOOK	SHELL_hHook = 0;
static UINT	uMsgWndCreated = 0;
static UINT	uMsgWndDestroyed = 0;
static UINT	uMsgShellActivate = 0;

LPCSTR krnl386_search_executable_file(LPCSTR lpFile, LPSTR buf, SIZE_T size, BOOL search_builtin);
static HICON convert_icon_to_32( HICON16 icon16 )
{
	if (!icon16) return 0;
    CURSORICONINFO *info = GlobalLock16( icon16 );
    void *and_bits = info + 1;
    void *xor_bits = (BYTE *)and_bits + info->nHeight * 2 * ((info->nWidth + 15) / 16);
    HICON ret = CreateIcon( 0, info->nWidth, info->nHeight, info->bPlanes, info->bBitsPerPixel,
                            and_bits, xor_bits );
    GlobalUnlock16( icon16 );
    return ret;
}

static HICON16 convert_icon_to_16( HINSTANCE16 inst, HICON icon )
{
	if (!icon) return 0;
    static HICON16 (WINAPI *pCreateIcon16)(HINSTANCE16,INT16,INT16,BYTE,BYTE,LPCVOID,LPCVOID);
    ICONINFO info;
    BITMAP bm;
    UINT and_size, xor_size;
    void *xor_bits = NULL, *and_bits;
    HICON16 handle = 0;

    if (!pCreateIcon16 &&
        !(pCreateIcon16 = (void *)GetProcAddress( GetModuleHandleA("user.exe16"), "CreateIcon16" )))
        return 0;

    if (!(GetIconInfo( icon, &info ))) return 0;
    GetObjectW( info.hbmMask, sizeof(bm), &bm );
    and_size = bm.bmHeight * bm.bmWidthBytes;
    if (!(and_bits = HeapAlloc( GetProcessHeap(), 0, and_size ))) goto done;
    GetBitmapBits( info.hbmMask, and_size, and_bits );
    if (info.hbmColor)
    {
        GetObjectW( info.hbmColor, sizeof(bm), &bm );
        xor_size = bm.bmHeight * bm.bmWidthBytes;
        if (!(xor_bits = HeapAlloc( GetProcessHeap(), 0, xor_size ))) goto done;
        GetBitmapBits( info.hbmColor, xor_size, xor_bits );
    }
    else
    {
        bm.bmHeight /= 2;
        xor_bits = (char *)and_bits + and_size / 2;
    }
    handle = pCreateIcon16( inst, bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel,
                            and_bits, xor_bits );
done:
    HeapFree( GetProcessHeap(), 0, and_bits );
    if (info.hbmColor)
    {
        HeapFree( GetProcessHeap(), 0, xor_bits );
        DeleteObject( info.hbmColor );
    }
    DeleteObject( info.hbmMask );
    DestroyIcon( icon );
    return handle;
}

/***********************************************************************
 * DllEntryPoint [SHELL.101]
 *
 * Initialization code for shell.dll. Automatically loads the
 * 32-bit shell32.dll to allow thunking up to 32-bit code.
 *
 * RETURNS
 *  Success: TRUE. Initialization completed successfully.
 *  Failure: FALSE.
 */
BOOL WINAPI SHELL_DllEntryPoint(DWORD Reason, HINSTANCE16 hInst,
				WORD ds, WORD HeapSize, DWORD res1, WORD res2)
{
    return TRUE;
}

/*************************************************************************
 *				DragAcceptFiles		[SHELL.9]
 */
void WINAPI DragAcceptFiles16(HWND16 hWnd, BOOL16 b)
{
  DragAcceptFiles(HWND_32(hWnd), b);
}

/*************************************************************************
 *				DragQueryFile		[SHELL.11]
 */
UINT16 WINAPI DragQueryFile16(
	HDROP16 hDrop,
	WORD wFile,
	LPSTR lpszFile,
	WORD wLength)
{
 	LPSTR lpDrop;
	UINT i = 0;
	LPDROPFILESTRUCT16 lpDropFileStruct = (LPDROPFILESTRUCT16) GlobalLock16(hDrop);

	TRACE("(%04x, %x, %p, %u)\n", hDrop,wFile,lpszFile,wLength);

	if(!lpDropFileStruct) goto end;

	lpDrop = (LPSTR) lpDropFileStruct + lpDropFileStruct->wSize;

	while (i++ < wFile)
	{
	  while (*lpDrop++); /* skip filename */
	  if (!*lpDrop)
	  {
	    i = (wFile == 0xFFFF) ? i : 0;
	    goto end;
	  }
	}

	i = strlen(lpDrop);
	if (!lpszFile ) goto end;   /* needed buffer size */
	lstrcpynA (lpszFile, lpDrop, wLength);
end:
	GlobalUnlock16(hDrop);
	return i;
}

/*************************************************************************
 *				DragFinish		[SHELL.12]
 */
void WINAPI DragFinish16(HDROP16 h)
{
    TRACE("\n");
    GlobalFree16(h);
}


/*************************************************************************
 *				DragQueryPoint		[SHELL.13]
 */
BOOL16 WINAPI DragQueryPoint16(HDROP16 hDrop, POINT16 *p)
{
  LPDROPFILESTRUCT16 lpDropFileStruct;
  BOOL16           bRet;
  TRACE("\n");
  lpDropFileStruct = (LPDROPFILESTRUCT16) GlobalLock16(hDrop);

  memcpy(p,&lpDropFileStruct->ptMousePos,sizeof(POINT16));
  bRet = lpDropFileStruct->fInNonClientArea;

  GlobalUnlock16(hDrop);
  return bRet;
}

/*************************************************************************
 *             FindExecutable   (SHELL.21)
 */
HINSTANCE16 WINAPI FindExecutable16( LPCSTR lpFile, LPCSTR lpDirectory,
                                     LPSTR lpResult )
{
    HINSTANCE hInst = FindExecutableA( lpFile, lpDirectory, lpResult );
    if (hInst == ERROR_FILE_NOT_FOUND)
    {
        if (!lpDirectory || !*lpDirectory)
        {
            char buf[MAX_PATH];
            if (krnl386_search_executable_file(lpFile, buf, ARRAYSIZE(buf), TRUE) == buf)
            {
                hInst = FindExecutableA(buf, NULL, lpResult);
            }
        }
    }
    GetShortPathNameA(lpResult, lpResult, strlen(lpResult) + 1);
    return HINSTANCE_16(hInst);
}

/*************************************************************************
 *             AboutDlgProc   (SHELL.33)
 */
BOOL16 WINAPI AboutDlgProc16( HWND16 hWnd, UINT16 msg, WPARAM16 wParam,
                               LPARAM lParam )
{
    FIXME( "stub\n" );
    return FALSE;
}


/*************************************************************************
 *             ShellAbout   (SHELL.22)
 */
BOOL16 WINAPI ShellAbout16( HWND16 hWnd, LPCSTR szApp, LPCSTR szOtherStuff, HICON16 icon16 )
{
    HICON icon = convert_icon_to_32( icon16 );
    BOOL ret = ShellAboutA( HWND_32(hWnd), szApp, szOtherStuff, icon );
    DestroyIcon( icon );
    return ret;
}


__declspec(dllimport) HICON NE_ExtractIcon(LPCSTR lpszExeFileName,
	HICON * RetPtr,
	INT nIconIndex,
	UINT nIcons,
	UINT cxDesired,
	UINT cyDesired,
	UINT *pIconId,
	UINT flags);
static inline UINT PrivateExtractIconsNE(
	LPCSTR lpwstrFile,
	int nIndex,
	int sizeX,
	int sizeY,
	HICON * phicon, /* [out] pointer to array of nIcons HICON handles */
	UINT* pIconId,  /* [out] pointer to array of nIcons icon identifiers or NULL */
	UINT nIcons,    /* [in] number of icons to retrieve */
	UINT flags)    /* [in] LR_* flags used by LoadImage */
{
	HICON ret;
	return (ret = PrivateExtractIconsA(lpwstrFile, nIndex, sizeX, sizeY, phicon, pIconId, nIcons, flags))
		? ret : NE_ExtractIcon(lpwstrFile, phicon, nIndex, nIcons, sizeX, sizeY, pIconId, flags);
}
/*************************************************************************
 *			InternalExtractIcon		[SHELL.39]
 *
 * This abortion is called directly by Progman
 */
HGLOBAL16 WINAPI InternalExtractIcon16(HINSTANCE16 hInstance,
                                     LPCSTR lpszExeFileName, UINT16 nIconIndex, WORD n )
{
    HGLOBAL16 hRet = 0;
    HICON16 *RetPtr = NULL;

	TRACE("(%04x,file %s,start %d,extract %d\n",
		       hInstance, lpszExeFileName, nIconIndex, n);

	if (!n)
	  return 0;

	hRet = GlobalAlloc16(GMEM_FIXED | GMEM_ZEROINIT, sizeof(*RetPtr) * n);
        RetPtr = GlobalLock16(hRet);

	if (nIconIndex == (UINT16)-1)  /* get number of icons */
	{
	  RetPtr[0] = PrivateExtractIconsNE(lpszExeFileName, -1, 0, 0, NULL, NULL, 0, LR_DEFAULTCOLOR);
	}
	else
	{
	  UINT ret;
	  HICON *icons;

	  icons = HeapAlloc(GetProcessHeap(), 0, n * sizeof(*icons));
	  ret = PrivateExtractIconsNE(lpszExeFileName, nIconIndex,
	                             GetSystemMetrics(SM_CXICON),
	                             GetSystemMetrics(SM_CYICON),
	                             icons, NULL, n, LR_DEFAULTCOLOR);
	  if ((ret != 0xffffffff) && ret)
	  {
	    int i;
	    for (i = 0; i < n; i++) RetPtr[i] = convert_icon_to_16(hInstance, icons[i]);
	  }
	  else
	  {
	    GlobalFree16(hRet);
	    hRet = 0;
	  }
	  HeapFree(GetProcessHeap(), 0, icons);
	}
	return hRet;
}

/*************************************************************************
* ExtractIconW                [SHELL32.@]
*/
HICON WINAPI ExtractIconNE(HINSTANCE hInstance, LPCSTR lpszFile, UINT nIconIndex)
{
	HICON  hIcon = NULL;
	UINT ret;
	UINT cx = GetSystemMetrics(SM_CXICON), cy = GetSystemMetrics(SM_CYICON);

	TRACE("%p %s %d\n", hInstance, debugstr_a(lpszFile), nIconIndex);

	if (nIconIndex == (UINT)-1)
	{
		ret = PrivateExtractIconsNE(lpszFile, -1, cx, cy, NULL, NULL, 0, LR_DEFAULTCOLOR);
		if (ret != (UINT)-1 && ret)
			return (HICON)(UINT_PTR)ret;
		return NULL;
	}
	else
		ret = PrivateExtractIconsNE(lpszFile, nIconIndex, cx, cy, &hIcon, NULL, 1, LR_DEFAULTCOLOR);

	if (ret == (UINT)-1)
		return (HICON)1;
	else if (ret > 0 && hIcon)
		return hIcon;

	return NULL;
}
__declspec(dllimport) LPCSTR RedirectSystemDir(LPCSTR path, LPSTR to, size_t max_len);
/*************************************************************************
 *             ExtractIcon   (SHELL.34)
 */
HICON16 WINAPI ExtractIcon16( HINSTANCE16 hInstance, LPCSTR lpszExeFileName,
	UINT16 nIconIndex )
{
    char buf[MAX_PATH];
    TRACE("\n");
    lpszExeFileName = RedirectSystemDir(lpszExeFileName, buf, MAX_PATH);
    if (nIconIndex == 0xFFFF)
    {
	    return ExtractIconNE(NULL, lpszExeFileName, -1);
    }
    return convert_icon_to_16( hInstance, ExtractIconNE(NULL, lpszExeFileName, nIconIndex) );
}

/*************************************************************************
 *             ExtractIconEx   (SHELL.40)
 */
UINT16 WINAPI ExtractIconEx16(
	LPCSTR lpszFile, INT16 nIconIndex, HICON16 *phiconLarge,
	HICON16 *phiconSmall, UINT16 nIcons
) {
    HICON	*ilarge,*ismall;
    int		i, ret;

    if (phiconLarge)
    	ilarge = HeapAlloc(GetProcessHeap(),0,nIcons*sizeof(HICON));
    else
    	ilarge = NULL;
    if (phiconSmall)
    	ismall = HeapAlloc(GetProcessHeap(),0,nIcons*sizeof(HICON));
    else
    	ismall = NULL;
    ret = ExtractIconExA(lpszFile,nIconIndex,ilarge,ismall,nIcons);
    if (ilarge) {
	for (i=0;i<ret;i++)
	    phiconLarge[i] = convert_icon_to_16(0, ilarge[i]);
	HeapFree(GetProcessHeap(),0,ilarge);
    }
    if (ismall) {
	for (i=0;i<ret;i++)
	    phiconSmall[i] = convert_icon_to_16(0, ismall[i]);
	HeapFree(GetProcessHeap(),0,ismall);
    }
    return ret;
}

/*************************************************************************
*				ExtractAssociatedIconW (SHELL32.@)
*
* Return icon for given file (either from file itself or from associated
* executable) and patch parameters if needed.
*/
HICON16 WINAPI ExtractAssociatedIconNE(HINSTANCE hInst, LPSTR lpIconPath, LPWORD lpiIcon)
{
	HICON hIcon = NULL;
	WORD wDummyIcon = 0;

	TRACE("%p %s %p\n", hInst, debugstr_a(lpIconPath), lpiIcon);

	if (lpiIcon == NULL)
		lpiIcon = &wDummyIcon;

	hIcon = ExtractIcon16(hInst, lpIconPath, *lpiIcon);

	if (hIcon < (HICON)2)
	{
		return convert_icon_to_16(hInst, ExtractAssociatedIconA(hInst, lpIconPath, lpiIcon));
	}
	return hIcon;
}
/*************************************************************************
 *				ExtractAssociatedIcon	[SHELL.36]
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON16 WINAPI ExtractAssociatedIcon16(HINSTANCE16 hInst, LPSTR lpIconPath, LPWORD lpiIcon)
{
    return ExtractAssociatedIconNE(NULL, lpIconPath, lpiIcon);
}

/*************************************************************************
 *				FindEnvironmentString	[SHELL.38]
 *
 * Returns a pointer into the DOS environment... Ugh.
 */
static LPSTR SHELL_FindString(LPSTR lpEnv, LPCSTR entry)
{ UINT16 l;

  TRACE("\n");

  l = strlen(entry);
  for( ; *lpEnv ; lpEnv+=strlen(lpEnv)+1 )
  { if( strncasecmp(lpEnv, entry, l) )
      continue;
	if( !*(lpEnv+l) )
	    return (lpEnv + l); 		/* empty entry */
	else if ( *(lpEnv+l)== '=' )
	    return (lpEnv + l + 1);
    }
    return NULL;
}

/**********************************************************************/

SEGPTR WINAPI FindEnvironmentString16(LPCSTR str)
{ SEGPTR  spEnv;
  LPSTR lpEnv,lpString;
  TRACE("\n");

  spEnv = GetDOSEnvironment16();

  lpEnv = MapSL(spEnv);
  lpString = (spEnv)?SHELL_FindString(lpEnv, str):NULL;

    if( lpString )		/*  offset should be small enough */
	return spEnv + (lpString - lpEnv);
    return 0;
}

/*************************************************************************
 *              		DoEnvironmentSubst      [SHELL.37]
 *
 * Replace %KEYWORD% in the str with the value of variable KEYWORD
 * from "DOS" environment. If it is not found the %KEYWORD% is left
 * intact. If the buffer is too small, str is not modified.
 *
 * PARAMS
 *  str        [I] '\0' terminated string with %keyword%.
 *             [O] '\0' terminated string with %keyword% substituted.
 *  length     [I] size of str.
 *
 * RETURNS
 *  str length in the LOWORD and 1 in HIWORD if subst was successful.
 */
DWORD WINAPI DoEnvironmentSubst16(LPSTR str,WORD length)
{
  LPSTR   lpEnv = MapSL(GetDOSEnvironment16());
  LPSTR   lpstr = str;
  LPSTR   lpend;
  LPSTR   lpBuffer = HeapAlloc( GetProcessHeap(), 0, length);
  WORD    bufCnt = 0;
  WORD    envKeyLen;
  LPSTR   lpKey;
  WORD    retStatus = 0;
  WORD    retLength = length;

  CharToOemA(str,str);

  TRACE("accept %s\n", str);

  while( *lpstr && bufCnt <= length - 1 ) {
     if ( *lpstr != '%' ) {
        lpBuffer[bufCnt++] = *lpstr++;
        continue;
     }

     for( lpend = lpstr + 1; *lpend && *lpend != '%'; lpend++) /**/;

     envKeyLen = lpend - lpstr - 1;
     if( *lpend != '%' || envKeyLen == 0)
        goto err; /* "%\0" or "%%" found; back off and whine */

     *lpend = '\0';
     lpKey = SHELL_FindString(lpEnv, lpstr+1);
     *lpend = '%';
     if( lpKey ) {
         int l = strlen(lpKey);

         if( bufCnt + l > length - 1 )
                goto err;

        memcpy(lpBuffer + bufCnt, lpKey, l);
        bufCnt += l;
     } else { /* Keyword not found; Leave the %KEYWORD% intact */
        if( bufCnt + envKeyLen + 2 > length - 1 )
            goto err;

         memcpy(lpBuffer + bufCnt, lpstr, envKeyLen + 2);
        bufCnt += envKeyLen + 2;
     }

     lpstr = lpend + 1;
  }

  if (!*lpstr && bufCnt <= length - 1) {
      memcpy(str,lpBuffer, bufCnt);
      str[bufCnt] = '\0';
      retLength = bufCnt + 1;
      retStatus = 1;
  }

  err:
  if (!retStatus)
      WARN("-- Env subst aborted - string too short or invalid input\n");
  TRACE("-- return %s\n", str);

  OemToCharA(str,str);
  HeapFree( GetProcessHeap(), 0, lpBuffer);

  return (DWORD)MAKELONG(retLength, retStatus);
}

/*************************************************************************
 *				SHELL_HookProc
 *
 * 32-bit version of the system-wide WH_SHELL hook.
 */
static LRESULT WINAPI SHELL_HookProc(INT code, WPARAM wParam, LPARAM lParam)
{
    TRACE("%i, %lx, %08lx\n", code, wParam, lParam );

    if (SHELL_hWnd)
    {
        switch( code )
        {
        case HSHELL_WINDOWCREATED:
            PostMessageA( SHELL_hWnd, uMsgWndCreated, wParam, 0 );
            break;
        case HSHELL_WINDOWDESTROYED:
            PostMessageA( SHELL_hWnd, uMsgWndDestroyed, wParam, 0 );
            break;
        case HSHELL_ACTIVATESHELLWINDOW:
            PostMessageA( SHELL_hWnd, uMsgShellActivate, wParam, 0 );
            break;
        }
    }
    return CallNextHookEx( SHELL_hHook, code, wParam, lParam );
}

/*************************************************************************
 *				ShellHookProc		[SHELL.103]
 * System-wide WH_SHELL hook.
 */
LRESULT WINAPI ShellHookProc16(INT16 code, WPARAM16 wParam, LPARAM lParam)
{
    return SHELL_HookProc( code, wParam, lParam );
}

/*************************************************************************
 *				RegisterShellHook	[SHELL.102]
 */
BOOL WINAPI RegisterShellHook16(HWND16 hWnd, UINT16 uAction)
{
    TRACE("%04x [%u]\n", hWnd, uAction );

    switch( uAction )
    {
    case 2:  /* register hWnd as a shell window */
        if( !SHELL_hHook )
        {
            SHELL_hHook = SetWindowsHookExA( WH_SHELL, SHELL_HookProc,
                                             GetModuleHandleA("shell32.dll"), 0 );
            if ( SHELL_hHook )
            {
                uMsgWndCreated = RegisterWindowMessageA( lpstrMsgWndCreated );
                uMsgWndDestroyed = RegisterWindowMessageA( lpstrMsgWndDestroyed );
                uMsgShellActivate = RegisterWindowMessageA( lpstrMsgShellActivate );
            }
            else
                WARN("-- unable to install ShellHookProc()!\n");
        }

        if ( SHELL_hHook )
            return ((SHELL_hWnd = HWND_32(hWnd)) != 0);
        break;

    default:
        WARN("-- unknown code %i\n", uAction );
        SHELL_hWnd = 0; /* just in case */
    }
    return FALSE;
}


/***********************************************************************
 *           DriveType   (SHELL.262)
 */
UINT16 WINAPI DriveType16( UINT16 drive )
{
    UINT ret;
    char path[] = "A:\\";
    path[0] += drive;
    ret = GetDriveTypeA(path);
    switch(ret)  /* some values are not supported in Win16 */
    {
    case DRIVE_CDROM:
        ret = DRIVE_REMOTE;
        break;
    case DRIVE_NO_ROOT_DIR:
        ret = DRIVE_UNKNOWN;
        break;
    }
    return ret;
}

BOOL is_reactos_or_wine()
{
    static BOOL detected;
    static BOOL is;
    HKEY hKey;
    CHAR name[100];
    DWORD dwType, dwSize = sizeof(name);
    if (detected)
        return is;
    detected = TRUE;
    if (ERROR_SUCCESS != RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE, &hKey))
        return FALSE;
    if (ERROR_SUCCESS != RegQueryValueExA(hKey, "ProductName", NULL, &dwType, (LPBYTE)name, &dwSize))
    {
        RegCloseKey(hKey);
        return FALSE;
    }
    RegCloseKey(hKey);
    if (dwType != REG_SZ)
        return FALSE;
    is = strstr(name, "ReactOS") != NULL;
    if (!is)
    {
        is = GetProcAddress(GetModuleHandleW("ntdll"), "wine_get_version") != NULL;
    }
    return is;
}

/*************************************************************************
 *           SHELL_Execute16 [Internal]
 */
static UINT_PTR SHELL_Execute16(const WCHAR *lpCmd, WCHAR *env, BOOL shWait,
			    const SHELLEXECUTEINFOW *psei, LPSHELLEXECUTEINFOW psei_out)
{
    UINT ret;
    char sCmd[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, lpCmd, -1, sCmd, MAX_PATH, NULL, NULL);
    ret = WinExec16(sCmd, (UINT16)psei->nShow);
    psei_out->hInstApp = HINSTANCE_32(ret);
    return ret;
}

static UINT_PTR WINAPI SHELL_Execute16_Windows(const CHAR *lpCmd, int nShowCmd, const CHAR *lpDir)
{
	UINT ret;
	char currdir[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, currdir);
	SetCurrentDirectoryA(lpDir);
	ret = WinExec16(lpCmd, nShowCmd);
	SetCurrentDirectoryA(currdir);
	return ret;
}

/*************************************************************************
 *                              ShellExecute            [SHELL.20]
 */
HINSTANCE16 WINAPI ShellExecute16( HWND16 hWnd, LPCSTR lpOperation,
                                   LPCSTR lpFile, LPCSTR lpParameters,
                                   LPCSTR lpDirectory, INT16 iShowCmd )
{
    void *callback = is_reactos_or_wine() ? (void*)SHELL_Execute16 : (void*)SHELL_Execute16_Windows;
    HINSTANCE rret1632 = WOWShellExecute(HWND_32(hWnd), lpOperation, lpFile, lpParameters,
        lpDirectory, iShowCmd, callback);
    if (rret1632 >= 32)
    {
        return HINSTANCE_16(rret1632);
    }
    if (rret1632 == ERROR_FILE_NOT_FOUND)
    {
        return rret1632;
    }
	HINSTANCE rret = ShellExecuteA(HWND_32(hWnd), lpOperation, lpFile, lpParameters,
		lpDirectory, iShowCmd);
	if (GetLastError() == 0x000000d8)
	{
        return (HINSTANCE16)rret;
	}
	else
	{
		if (rret >= 32)
			return HINSTANCE_16(rret);
		return (HINSTANCE16)rret;
	}
}


HMODULE16 WINAPI MapHModuleLS(HMODULE hmod);
/*************************************************************************
 * RunDLL_CallEntry16
 *
 * Only exported from shell32 on Windows, probably imported
 * from shell through the 16/32 thunks.
 */
void WINAPI RunDLL_CallEntry16( DWORD proc, HWND hwnd, HINSTANCE inst, LPCSTR cmdline, INT cmdshow )
{
    WORD args[5];
    SEGPTR cmdline_seg;

    TRACE( "proc %x hwnd %p inst %p cmdline %s cmdshow %d\n",
           proc, hwnd, inst, debugstr_a(cmdline), cmdshow );

    cmdline_seg = MapLS( cmdline );
    args[4] = HWND_16(hwnd);
    args[3] = MapHModuleLS(inst);
    args[2] = SELECTOROF(cmdline_seg);
    args[1] = OFFSETOF(cmdline_seg);
    args[0] = cmdshow;
    WOWCallback16Ex( proc, WCB16_PASCAL, sizeof(args), args, NULL );
    UnMapLS( cmdline_seg );
}

DWORD WINAPI SHFormatDrive16(HWND16 hwnd, UINT16 drive, UINT16 fmtID, UINT16 options)
{
    return SHFormatDrive(HWND_32(hwnd), drive, fmtID, options);
}
