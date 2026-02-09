// win_api.h -- Minimal Win32 API declarations for DTT -fsyntax-only testing.
// No Windows SDK required. These are just function signatures for DTT to match.
#pragma once

typedef void*          HANDLE;
typedef int            BOOL;
typedef unsigned long  DWORD;
typedef unsigned int   UINT;
typedef long           LONG;
typedef const wchar_t* LPCWSTR;
typedef const char*    LPCSTR;
typedef wchar_t*       BSTR;
typedef unsigned short VARTYPE;
typedef void*          PVOID;
typedef HANDLE         HKEY;
typedef HANDLE         HDC;
typedef HANDLE         HGDIOBJ;

struct SECURITY_ATTRIBUTES { DWORD nLength; void* lpSecurityDescriptor; BOOL bInheritHandle; };
struct WIN32_FIND_DATAW { DWORD dwFileAttributes; wchar_t cFileName[260]; };
struct WIN32_FIND_DATAA { DWORD dwFileAttributes; char cFileName[260]; };
struct SAFEARRAYBOUND { DWORD cElements; LONG lLbound; };
struct SAFEARRAY { VARTYPE vt; UINT cDims; };
struct VARIANT { VARTYPE vt; };

extern "C" {

// --- HANDLE lifecycle ---
HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, SECURITY_ATTRIBUTES*, DWORD, DWORD, HANDLE);
HANDLE CreateFileA(LPCSTR,  DWORD, DWORD, SECURITY_ATTRIBUTES*, DWORD, DWORD, HANDLE);
BOOL   CloseHandle(HANDLE);

// --- File search ---
HANDLE FindFirstFileW(LPCWSTR, WIN32_FIND_DATAW*);
HANDLE FindFirstFileA(LPCSTR,  WIN32_FIND_DATAA*);
BOOL   FindNextFileW(HANDLE, WIN32_FIND_DATAW*);
BOOL   FindNextFileA(HANDLE, WIN32_FIND_DATAA*);
BOOL   FindClose(HANDLE);

// --- Registry ---
LONG RegOpenKeyExW(HKEY, LPCWSTR, DWORD, DWORD, HKEY*);
LONG RegCloseKey(HKEY);

// --- GDI ---
HDC     CreateCompatibleDC(HDC);
BOOL    DeleteDC(HDC);
BOOL    DeleteObject(HGDIOBJ);

// --- Synchronization ---
HANDLE CreateEventA(SECURITY_ATTRIBUTES*, BOOL, BOOL, LPCSTR);
HANDLE CreateEventW(SECURITY_ATTRIBUTES*, BOOL, BOOL, LPCWSTR);
HANDLE CreateMutexA(SECURITY_ATTRIBUTES*, BOOL, LPCSTR);
HANDLE CreateMutexW(SECURITY_ATTRIBUTES*, BOOL, LPCWSTR);
HANDLE CreateSemaphoreA(SECURITY_ATTRIBUTES*, LONG, LONG, LPCSTR);
HANDLE CreateSemaphoreW(SECURITY_ATTRIBUTES*, LONG, LONG, LPCWSTR);

// --- Heap ---
HANDLE HeapCreate(DWORD, DWORD, DWORD);
BOOL   HeapDestroy(HANDLE);
PVOID  HeapAlloc(HANDLE, DWORD, DWORD);
BOOL   HeapFree(HANDLE, DWORD, PVOID);

// --- Memory-mapped files ---
HANDLE CreateFileMappingA(HANDLE, SECURITY_ATTRIBUTES*, DWORD, DWORD, DWORD, LPCSTR);
HANDLE CreateFileMappingW(HANDLE, SECURITY_ATTRIBUTES*, DWORD, DWORD, DWORD, LPCWSTR);
PVOID  MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, DWORD);
BOOL   UnmapViewOfFile(PVOID);

// --- BSTR ---
BSTR SysAllocString(const wchar_t*);
BSTR SysAllocStringLen(const wchar_t*, UINT);
BSTR SysAllocStringByteLen(const char*, UINT);
void SysFreeString(BSTR);

// --- SafeArray ---
SAFEARRAY* SafeArrayCreate(VARTYPE, UINT, SAFEARRAYBOUND*);
SAFEARRAY* SafeArrayCreateVector(VARTYPE, LONG, UINT);
long SafeArrayDestroy(SAFEARRAY*);
long SafeArrayAccessData(SAFEARRAY*, void**);
long SafeArrayUnaccessData(SAFEARRAY*);

// --- VARIANT ---
void VariantInit(VARIANT*);
long VariantClear(VARIANT*);

// --- Allocators ---
void* CoTaskMemAlloc(unsigned long);
void  CoTaskMemFree(void*);
void* LocalAlloc(UINT, unsigned long);
void* LocalFree(void*);
void* GlobalAlloc(UINT, unsigned long);
void* GlobalFree(void*);

}
