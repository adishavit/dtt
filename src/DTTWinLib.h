// DTTWinLib.h - Windows API function annotations for DTT
//
// Provides semantic annotations for Win32 API functions:
// - HANDLE lifecycle: CreateFile/CloseHandle, FindFirstFile/FindClose, GDI, sync
// - COM/OLE allocators: SysAllocString/SysFreeString, CoTaskMem, Local, Global
//
// Always registered (annotations for absent functions are inert).

#ifndef DTT_WINLIB_H
#define DTT_WINLIB_H

namespace dtt {

// Register all Windows API function annotations
void registerWindowsAnnotations();

} // namespace dtt

#endif // DTT_WINLIB_H
