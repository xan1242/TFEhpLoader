//
// Tag Force 3 Memory EHP loader
// Loads EHPs into memory and overrides embedded ones in the executable
// 
// by xan1242 / Tenjoin 
// 
// EHP loader & game hook code 
// 

#include <pspsdk.h>
#include <pspuser.h>
#include <stdio.h>
#include <kubridge.h>
#include "../../includes/psp/injector.h"
#include "ehploader.h"
#include "../../includes/psp/pspmallochelper.h"

// undefine for debug logging via sceKernelPrintf
//#define EHPLOADER_DEBUG_PRINTS

void (*EhFolder_CreateFromMemory)(int unk, void* ehppointer) = (void (*)(int, void*))FUNC_ADDR_CREATEFROMMEMORY;
uintptr_t(*EhFolder_SearchFile)(void* ehfolder, char* filename, int unk) = (uintptr_t(*)(void*, char*, int))FUNC_ADDR_SEARCHFILE;

uintptr_t base_addr = 0;
char basePath[128];

// don't allocate space for EHP_TYPE_UNK
void* ptrEhpFiles[EHP_TYPE_COUNT - 1];

void* LoadFileToMem(const char* path)
{
    SceIoStat st = { 0 };
    int res = sceIoGetstat(path, &st);
    if (res < 0)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf("sceIoGetstat fail! 0x%X\n", res);
#endif
        return NULL;
    }
    void* out = psp_malloc(st.st_size);
    if (out == NULL)
        return NULL;

    SceUID f = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (f < 0)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf("sceIoOpen fail! 0x%X\n", f);
#endif
        return NULL;
    }
    sceIoRead(f, out, st.st_size);
    sceIoClose(f);

    return out;
}

EhpType DetectType(void* addr)
{
    uintptr_t iaddr = (uintptr_t)addr;
    iaddr -= base_addr;

    switch (iaddr)
    {
    case EHP_ADDR_CNAME_BIN:
        return EHP_TYPE_CNAME;
    case EHP_ADDR_INTERFACE_BIN:
        return EHP_TYPE_INTERFACE;
    case EHP_ADDR_RCPSET_BIN:
        return EHP_TYPE_RCPSET;
    case EHP_ADDR_LOAD_FL_BIN:
        return EHP_TYPE_LOAD_FL;
    case EHP_ADDR_SYSMSG_BIN:
        return EHP_TYPE_SYSMSG;
    case EHP_ADDR_PACKSET_BIN:
        return EHP_TYPE_PACKSET;

    default:
        return EHP_TYPE_UNK;
    }

    return EHP_TYPE_UNK;
}

const char* GetTypeFilename(EhpType type)
{
    switch (type)
    {
    case EHP_TYPE_CNAME:
        return EHP_NAME_CNAME;
    case EHP_TYPE_INTERFACE:
        return EHP_NAME_INTERFACE;
    case EHP_TYPE_RCPSET:
        return EHP_NAME_RCPSET;
    case EHP_TYPE_LOAD_FL:
        return EHP_NAME_LOAD_FL;
    case EHP_TYPE_SYSMSG:
        return EHP_NAME_SYSMSG;
    case EHP_TYPE_PACKSET:
        return EHP_NAME_PACKSET;
    default:
        return NULL;
    }
    return NULL;
}

void* GetEhpPtr(EhpType type)
{
    if (type == EHP_TYPE_UNK)
        return NULL;
    return ptrEhpFiles[type - 1];
}

void EhFolder_CreateFromMemory_Hook(int unk, void* ptr)
{
    EhpType type = DetectType(ptr);
    if (type == EHP_TYPE_UNK)
        return EhFolder_CreateFromMemory(unk, ptr);

    void* ptrNewEhp = GetEhpPtr(type);
    if (ptrNewEhp != NULL)
        return EhFolder_CreateFromMemory(unk, ptrNewEhp);

    const char* filename = GetTypeFilename(type);

    size_t basePathLen = strlen(basePath);
    size_t filenameLen = strlen(filename);
    size_t totalLen = basePathLen + filenameLen + 1;
    char* completePath = (char*)psp_malloc(totalLen);
    if (completePath == NULL)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf("String malloc failure!");
#endif
        return EhFolder_CreateFromMemory(unk, ptr);
    }
    strcpy(completePath, basePath);
    strcat(completePath, filename);

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("Loading: %s", completePath);
#endif
    void* memFile = LoadFileToMem(completePath);
    if (memFile == NULL)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf("LoadFileToMem failure!");
#endif
        psp_free(completePath);
        return EhFolder_CreateFromMemory(unk, ptr);
    }
#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("Load successful");
#endif

    ptrEhpFiles[type - 1] = memFile;
    psp_free(completePath);

    return EhFolder_CreateFromMemory(unk, memFile);
}

uintptr_t EhFolder_SearchFile_Hook(void* ptr, char* filename, int unk)
{
    EhpType type = DetectType(ptr);
    if (type == EHP_TYPE_UNK)
        return EhFolder_SearchFile(ptr, filename, unk);

    void* ptrNewEhp = GetEhpPtr(type);
    if (ptrNewEhp == NULL)
        return EhFolder_SearchFile(ptr, filename, unk);
#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("SearchFile: %s/%s", GetTypeFilename(type), filename);
#endif

    return EhFolder_SearchFile(ptrNewEhp, filename, unk);
}

void EhpLoaderInject(const char* folderPath)
{
    strcpy(basePath, folderPath);
    base_addr = injector.base_addr;

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("BaseAddr: 0x%X", base_addr);
    sceKernelPrintf("BasePath: %s", basePath);
#endif

    // update func addresses
    EhFolder_CreateFromMemory = (void (*)(int, void*))(FUNC_ADDR_CREATEFROMMEMORY + base_addr);
    EhFolder_SearchFile = (uintptr_t(*)(void*, char*, int))(FUNC_ADDR_SEARCHFILE + base_addr);

    injector.MakeCALL(HOOK_ADDR_CREATE_CNAME,       (uintptr_t)&EhFolder_CreateFromMemory_Hook);
    injector.MakeCALL(HOOK_ADDR_CREATE_INTERFACE,   (uintptr_t)&EhFolder_CreateFromMemory_Hook);
    injector.MakeCALL(HOOK_ADDR_CREATE_RCPSET,      (uintptr_t)&EhFolder_CreateFromMemory_Hook);
    injector.MakeCALL(HOOK_ADDR_CREATE_LOAD_FL,     (uintptr_t)&EhFolder_CreateFromMemory_Hook);
    injector.MakeCALL(HOOK_ADDR_CREATE_SYSMSG,      (uintptr_t)&EhFolder_CreateFromMemory_Hook);
    injector.MakeCALL(HOOK_ADDR_CREATE_PACKSET,     (uintptr_t)&EhFolder_CreateFromMemory_Hook);

    injector.MakeCALL(HOOK_ADDR_1_SEARCH_CNAME,     (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_1_SEARCH_INTERFACE, (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_2_SEARCH_INTERFACE, (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_3_SEARCH_INTERFACE, (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_4_SEARCH_INTERFACE, (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_5_SEARCH_INTERFACE, (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_1_SEARCH_RCPSET,    (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_2_SEARCH_RCPSET,    (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_3_SEARCH_RCPSET,    (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_1_SEARCH_LOAD_FL,   (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_1_SEARCH_SYSMSG,    (uintptr_t)&EhFolder_SearchFile_Hook);
    injector.MakeCALL(HOOK_ADDR_1_SEARCH_PACKSET,   (uintptr_t)&EhFolder_SearchFile_Hook);
}
