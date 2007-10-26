/*
 * win32_crashrpt_dll.h : private header file.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_SUBR_WIN32_CRASHRPT_DLL_H
#define SVN_LIBSVN_SUBR_WIN32_CRASHRPT_DLL_H

#ifdef WIN32
#ifdef SVN_USE_WIN32_CRASHHANDLER

/* public functions in dbghelp.dll */
typedef BOOL  (WINAPI * MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD ProcessId,
               HANDLE hFile, MINIDUMP_TYPE DumpType,
               CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
               CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
               CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
typedef BOOL  (WINAPI * SYMINITIALIZE)(HANDLE hProcess, PSTR UserSearchPath,
                                    BOOL fInvadeProcess);
typedef DWORD (WINAPI * SYMSETOPTIONS)(DWORD SymOptions);

typedef DWORD (WINAPI * SYMGETOPTIONS)(VOID);

typedef BOOL  (WINAPI * SYMCLEANUP)(HANDLE hProcess);

typedef BOOL  (WINAPI * SYMGETTYPEINFO)(HANDLE hProcess, DWORD64 ModBase,
                                     ULONG TypeId, IMAGEHLP_SYMBOL_TYPE_INFO GetType,
                                     PVOID pInfo);

typedef BOOL  (WINAPI * SYMGETLINEFROMADDR64)(HANDLE hProcess, DWORD64 dwAddr,
                                 PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line);

typedef BOOL  (WINAPI * SYMENUMSYMBOLS)(HANDLE hProcess, ULONG64 BaseOfDll, PCSTR Mask,
                             PSYM_ENUMERATESYMBOLS_CALLBACK EnumSymbolsCallback,
                             PVOID UserContext);

typedef BOOL  (WINAPI * SYMSETCONTEXT)(HANDLE hProcess, PIMAGEHLP_STACK_FRAME StackFrame,
                            PIMAGEHLP_CONTEXT Context);

typedef BOOL  (WINAPI * SYMFROMADDR)(HANDLE hProcess, DWORD64 Address,
                          PDWORD64 Displacement, PSYMBOL_INFO Symbol);

typedef BOOL (WINAPI * STACKWALK64)(DWORD MachineType, HANDLE hProcess, HANDLE hThread,
                                LPSTACKFRAME64 StackFrame, PVOID ContextRecord,
                                PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
                                PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
                                PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
                                PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);

typedef PVOID (WINAPI * SYMFUNCTIONTABLEACCESS64)(HANDLE hProcess, DWORD64 AddrBase);

typedef DWORD64 (WINAPI * SYMGETMODULEBASE64)(HANDLE hProcess, DWORD64 dwAddr);

/* public functions in version.dll */
typedef DWORD (APIENTRY * GETFILEVERSIONINFOSIZE)
                          (LPCSTR lptstrFilename, LPDWORD lpdwHandle);
typedef BOOL  (APIENTRY * GETFILEVERSIONINFO)
                          (LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen,
                           LPVOID lpData);
typedef BOOL  (APIENTRY * VERQUERYVALUE)
                          (const LPVOID pBlock, LPSTR lpSubBlock,
                           LPVOID * lplpBuffer, PUINT puLen);

/* public functions in kernel32.dll */
typedef BOOL  (WINAPI * ISDEBUGGERPRESENT)(VOID);

/* function pointers */
MINIDUMPWRITEDUMP        MiniDumpWriteDump_;
SYMINITIALIZE            SymInitialize_;
SYMSETOPTIONS            SymSetOptions_;
SYMGETOPTIONS            SymGetOptions_;
SYMCLEANUP               SymCleanup_;
SYMGETTYPEINFO           SymGetTypeInfo_;
SYMGETLINEFROMADDR64     SymGetLineFromAddr64_;
SYMENUMSYMBOLS           SymEnumSymbols_;
SYMSETCONTEXT            SymSetContext_;
SYMFROMADDR              SymFromAddr_;
STACKWALK64              StackWalk64_;
SYMFUNCTIONTABLEACCESS64 SymFunctionTableAccess64_;
SYMGETMODULEBASE64       SymGetModuleBase64_;

#endif /* SVN_USE_WIN32_CRASHHANDLER */
#endif /* WIN32 */

#endif /* SVN_LIBSVN_SUBR_WIN32_CRASHRPT_DLL_H */