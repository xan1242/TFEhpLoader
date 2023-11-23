//
// Tag Force Memory EHP loader
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
#include "../../includes/psp/patterns.h"
#include "ehploader.h"
#include "../../includes/psp/pspmallochelper.h"

// undefine for debug logging via sceKernelPrintf
//#define EHPLOADER_DEBUG_PRINTS

void (*EhFolder_CreateFromMemory)(int unk, void* ehppointer) = (void (*)(int, void*))0x1DF40;
uintptr_t(*lEhFolder_SearchFile)(uintptr_t pEhFolder, char* filename) = (uintptr_t(*)(uintptr_t, char*))0x1E270;
uint32_t(*lEhFolder_GetFileSizeSub)(uintptr_t ptr, uintptr_t handle) = (uint32_t(*)(uintptr_t, uintptr_t))0x1E600;
void(*YgSys_Ms_GetDirName)(char* out) = (void(*)(char*))0x61548; // address in TF1 JP


uintptr_t base_addr = 0;
char basePath[128];
char GameSerial[32];

void* ptrEhpFiles[EHP_TYPE_COUNT];
void* ptrEhpFilesOriginal[EHP_TYPE_COUNT];

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
    if (addr == NULL)
        return EHP_TYPE_UNK;

    for (int i = 0; i < EHP_TYPE_COUNT; i++)
    {
        if (addr == ptrEhpFilesOriginal[i])
            return (EhpType)i;
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
    return ptrEhpFiles[type];
}

void EhFolder_CreateFromMemory_Hook(int unk, void* ptr)
{
#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("EhFolder_CreateFromMemory (0x%X)", ptr);
#endif

    EhpType type = DetectType(ptr);

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("CreateFromMemory detected type: %d", type);
#endif
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

    ptrEhpFiles[type] = memFile;
    psp_free(completePath);

    EhFolder_CreateFromMemory(unk, memFile);

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("Load successful, addr: 0x%X", memFile);
#endif

    return;
}

int EhFolder_CheckMagic(uintptr_t ptr)
{
    if (!ptr)
        return 0;

    return (*(uint32_t*)(ptr) & 0xFFFFFF) == EHP_MAGIC;
}

// decompiled the original function because we can't do (proper) inline asm hooks
uintptr_t EhFolder_SearchFile(uintptr_t ptrMemEhFolder, char* filename, uintptr_t unk)
{
    uintptr_t hEhFolder; // handle
    off_t offFile; // file offset

    if (!ptrMemEhFolder || !EhFolder_CheckMagic(ptrMemEhFolder))
        return 0;
    hEhFolder = lEhFolder_SearchFile(ptrMemEhFolder, filename);
    if (!hEhFolder)
        return 0;
    if (unk)
        *(uint32_t*)unk = lEhFolder_GetFileSizeSub(ptrMemEhFolder, hEhFolder);

    offFile = *(uint32_t*)(hEhFolder + 4);
    return ptrMemEhFolder + offFile;
}


uintptr_t EhFolder_SearchFile_Hook(uintptr_t ptrMemEhFolder, char* filename, uintptr_t unk)
{
    EhpType type = DetectType((void*)ptrMemEhFolder);
    if (type == EHP_TYPE_UNK)
        return EhFolder_SearchFile(ptrMemEhFolder, filename, unk);

    void* ptrNewEhp = GetEhpPtr(type);
    if (ptrNewEhp == NULL)
        return EhFolder_SearchFile(ptrMemEhFolder, filename, unk);
#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("SearchFile: %s/%s", GetTypeFilename(type), filename);
#endif

    return EhFolder_SearchFile((uintptr_t)ptrNewEhp, filename, unk);
}

uintptr_t DiscoverPtr(uintptr_t start, uintptr_t* ptrLast)
{
    uintptr_t ptrLUI = 0;
    uintptr_t ptrADD = start;

    // find the first LUI after ADDIU
    for (int i = 0; i < 10; i++)
    {
        uint32_t ins = *(uint32_t*)(ptrADD + (4 * i)) & 0xFFFF0000;
        if (ins == 0x3C050000)
        {
            ptrLUI = ptrADD + (4 * i);
            break;
        }
    }

    if (ptrLUI == NULL)
        return NULL;


    ptrADD = NULL;

    // find the first ADDIU after LUI
    for (int i = 0; i < 10; i++)
    {
        uint32_t ins = *(uint32_t*)(ptrLUI + (4 * i)) & 0xFFFF0000;
        if (ins == 0x24A50000)
        {
            ptrADD = ptrLUI + (4 * i);
            break;
        }
    }

    if (ptrADD == NULL)
        return NULL;

    uint32_t insLUI = *(uint32_t*)ptrLUI;
    uint32_t insADD = *(uint32_t*)ptrADD;

    // construct the ptr from the instructions
    uint32_t part1 = (insLUI & 0xFFFF);
    uint32_t part2 = (insADD & 0xFFFF);
    // TODO: check for negative numbers, this is a signed number
    if (part2 > 0x7FFF)
        part1 -= 1;
    part1 <<= 16;

    uintptr_t retVal = part1 | part2;

    *ptrLast = ptrADD;

    return retVal;
}

uintptr_t FindInstruction(uintptr_t start, uint32_t instruction, uint32_t inscount)
{
    for (int i = 0; i < inscount; i++)
    {
        uint32_t ins = *(uint32_t*)(start + (4 * i));
        
        if (ins == instruction)
        {
            return start + (4 * i);
        }
    }

    return 0;
}

uintptr_t FindFirstJAL(uintptr_t start, uint32_t inscount)
{
    for (int i = 0; i < inscount; i++)
    {
        uint32_t ins = *(uint32_t*)(start + (4 * i));

        if ((ins >> 26) == 3)
        {
            return start + (4 * i);
        }
    }

    return 0;
}

uintptr_t GetJALDestination(uint32_t instruction)
{
    return (instruction & 0x03FFFFFF) << 2;
}

void EhpLoaderInject(const char* folderPath)
{
    //sceKernelDelayThread(100000);
    strcpy(basePath, folderPath);
    base_addr = injector.base_addr;

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("BaseAddr: 0x%X", base_addr);
    
    sceKernelPrintf("Searching functions");
#endif

    int bInTF6 = 0;
    
    uintptr_t ptr_lEhFolder_SearchFile = 0;
    uintptr_t ptr_lEhFolder_GetFileSizeSub = 0;
    uintptr_t ptr_EhFolder_CreateFromMemory = 0;
    uintptr_t ptr_EhFolder_SearchFile = 0;
    uintptr_t ptr_YgSys_InitApplication = 0;

    // detect game
    // we only care if it's TF6/Special or not because of the different compiler
    
    // try to find YgSys_Ms_GetDirPath and derive from that
    // get it from lYgSysDLFile_GetFileList via a universal pattern
    uintptr_t ptr_lYgSysDLFile_GetFileList_4998C = pattern.get_first("28 00 00 ? ? ? ? 60 01 06 ?", 0) + 7;

    // failsafe - no pattern = not in the right game
    if (ptr_lYgSysDLFile_GetFileList_4998C == 0)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf("ERROR: Cannot detect Tag Force! Exiting...");
#endif
        return;
    }
    
    // find the first call after the memset within 6 instructions
    uintptr_t ptr_ptr_YgSys_Ms_GetDirPath = FindFirstJAL(ptr_lYgSysDLFile_GetFileList_4998C, 6);

    uintptr_t ptr_YgSys_Ms_GetDirPath = GetJALDestination(*(uint32_t*)ptr_ptr_YgSys_Ms_GetDirPath);
    uintptr_t ptr_YgSys_Ms_GetDirName = GetJALDestination(*(uint32_t*)(ptr_YgSys_Ms_GetDirPath + 0x1C));
    
    YgSys_Ms_GetDirName = (void (*)(char*))(ptr_YgSys_Ms_GetDirName);
    
    YgSys_Ms_GetDirName(GameSerial);
    GameSerial[strlen(GameSerial) - 4] = '\0';

    strcat(basePath, GameSerial);
    strcat(basePath, "/");
#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("Detected game: %s", GameSerial);
    sceKernelPrintf("BasePath: %s", basePath);
#endif


    if ((strcmp(GameSerial, "ULJM05940") == 0) || (strcmp(GameSerial, "NPJH00142") == 0))
        bInTF6 = 1;

    if (bInTF6)
    {
        // find functions (TF6 & Special)
        ptr_lEhFolder_SearchFile = pattern.get_first("30 00 BD 27 F0 FF BD 27 00 00 BF AF ? ? ? ? 01 00 06 34", 0) + 4;
        ptr_lEhFolder_GetFileSizeSub = pattern.get_first("F0 FF BD 27 00 00 86 8C 25 38 A0 00 00 3E C5 7C", 0);
        //ptr_EhFolder_CreateFromMemory = pattern.get_first("F0 FF BD 27 25 30 A0 00 ? ? 05 3C ? ? A7 8C 00 00 B0 AF", 0);
        ptr_EhFolder_SearchFile = pattern.get_first("25 88 A0 00 08 00 B2 AF 0C 00 BF AF 0C 00 80 10", 0) - 0x10;
        ptr_YgSys_InitApplication = pattern.get_first("FF BD 27 ? ? 05 3C 25 20 00 00 60 00 B0 AF 64 00 B1 AF", 0) - 1;
    }
    else
    {
        // TODO: add autodetect for debug builds
        // find functions (TF1 - 5)
        ptr_lEhFolder_SearchFile = pattern.get_first("01 00 06 24 ? ? ? ? 21 30 00 00 F0 FF BD 27", 0) - 4;
        ptr_lEhFolder_GetFileSizeSub = pattern.get_first("F0 FF BD 27 0C 00 BF AF 08 00 B0 AF 00 00 82 8C 00 3E 42 7C", 0);
        //ptr_EhFolder_CreateFromMemory = pattern.get_first("F0 FF BD 27 0C 00 BF AF ? ? 02 3C ? ? 42 8C 03 00 40 50", 0);
        ptr_EhFolder_SearchFile = pattern.get_first("00 00 B0 AF 21 90 80 00 21 80 A0 00 05 00 40 12 21 88 C0 00", 0) - 0x10;
        ptr_YgSys_InitApplication = pattern.get_first("FF BD 27 ? ? 05 3C 0C 00 BF AF 21 20 00 00", 0) - 1;
    }

    // we can get this function from YgSys_InitApplication
    ptr_EhFolder_CreateFromMemory = GetJALDestination(*(uint32_t*)(FindFirstJAL(ptr_YgSys_InitApplication, 10)));


#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("lEhFolder_SearchFile: 0x%X", ptr_lEhFolder_SearchFile);
    sceKernelPrintf("lEhFolder_GetFileSizeSub: 0x%X", ptr_lEhFolder_GetFileSizeSub);
    sceKernelPrintf("EhFolder_CreateFromMemory: 0x%X", ptr_EhFolder_CreateFromMemory);
    sceKernelPrintf("EhFolder_SearchFile: 0x%X", ptr_EhFolder_SearchFile);
    sceKernelPrintf("YgSys_InitApplication: 0x%X", ptr_YgSys_InitApplication);

    sceKernelPrintf("Function search done");
#endif

    // update func addresses
    EhFolder_CreateFromMemory = (void (*)(int, void*))(ptr_EhFolder_CreateFromMemory);
    lEhFolder_SearchFile = (uintptr_t(*)(uintptr_t, char*))(ptr_lEhFolder_SearchFile );
    lEhFolder_GetFileSizeSub = (uint32_t(*)(uintptr_t, uintptr_t))(ptr_lEhFolder_GetFileSizeSub);

    // replace the original EhFolder_SearchFile
    injector.MakeJMPwNOP(ptr_EhFolder_SearchFile, (uintptr_t)&EhFolder_SearchFile_Hook);
    
    uint32_t numEHP = 1;
    uintptr_t ptrHookStart = 0;
    uint32_t insJAL = (0x0C000000 | ((ptr_EhFolder_CreateFromMemory >> 2) & 0x03FFFFFF));

    // find the first JAL within the first 10 instructions
    ptrHookStart = FindInstruction(ptr_YgSys_InitApplication, insJAL, 10);

    if (ptrHookStart == 0)
        return;

    // cname
    injector.MakeCALL(ptrHookStart, (uintptr_t)&EhFolder_CreateFromMemory_Hook);

    // hook the rest of the calls at the beginning of the function
    uintptr_t ptrJalHook = ptrHookStart + 4;
    do
    {
        // find the JAL within next 5 instructions
        ptrJalHook = FindInstruction(ptrJalHook, insJAL, 5);

        if (ptrJalHook)
        {
            injector.MakeCALL(ptrJalHook, (uintptr_t)&EhFolder_CreateFromMemory_Hook);
#ifdef EHPLOADER_DEBUG_PRINTS
            sceKernelPrintf("Hooking EhFolder_CreateFromMemory: 0x%X", ptrJalHook);
#endif
            ptrJalHook += 4;
            numEHP++;
        }
    } while (ptrJalHook);

    // get the EHP memory pointers & build the map
    uintptr_t ptrDiscoverStart = ptr_YgSys_InitApplication;
    uintptr_t EhpPtrs[10];
    for (int i = 0; i < numEHP; i++)
    {
        uintptr_t nextStart = 0;
        uintptr_t ptrEHP = DiscoverPtr(ptrDiscoverStart, &nextStart);

        EhpPtrs[i] = ptrEHP;

        ptrDiscoverStart = nextStart;
    }

    ptrEhpFilesOriginal[EHP_TYPE_CNAME] = (void*)EhpPtrs[EHP_TYPE_CNAME];
    ptrEhpFilesOriginal[EHP_TYPE_INTERFACE] = (void*)EhpPtrs[EHP_TYPE_INTERFACE];
    ptrEhpFilesOriginal[EHP_TYPE_RCPSET] = (void*)EhpPtrs[EHP_TYPE_RCPSET];

    if (numEHP < 6)
    {
        int bInTF1 = 0;

        // now we have to check in which TF game we're in
        // TF1 doesn't have the "packset" EhFolder, while TF4+ don't have "load_fl"

        // the last one in TF1 is always sysmsg, so we'll check for that
        uintptr_t ptrLastEHP = EhpPtrs[numEHP - 1];

        // check the first filename of the last EHP
        // get the pointer of the first filename
        char* EHPFirstFileName = (char*)((*(uint32_t*)(ptrLastEHP + 0x10)) + ptrLastEHP);

        if (strstr(EHPFirstFileName, "sysmsg"))
            bInTF1 = 1;

        if (bInTF1)
        {
            ptrEhpFilesOriginal[EHP_TYPE_LOAD_FL] = (void*)EhpPtrs[EHP_TYPE_LOAD_FL];
            ptrEhpFilesOriginal[EHP_TYPE_SYSMSG] = (void*)EhpPtrs[EHP_TYPE_SYSMSG];
        }
        else
        {
            ptrEhpFilesOriginal[EHP_TYPE_SYSMSG] = (void*)EhpPtrs[3];
            ptrEhpFilesOriginal[EHP_TYPE_PACKSET] = (void*)EhpPtrs[4];
        }
    }
    else
    {
        // these are in order of TF2 and 3
        ptrEhpFilesOriginal[EHP_TYPE_LOAD_FL] = (void*)EhpPtrs[EHP_TYPE_LOAD_FL];
        ptrEhpFilesOriginal[EHP_TYPE_SYSMSG] = (void*)EhpPtrs[EHP_TYPE_SYSMSG];
        ptrEhpFilesOriginal[EHP_TYPE_PACKSET] = (void*)EhpPtrs[EHP_TYPE_PACKSET];
    }

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf("===EhFolder ptrs:===");
    sceKernelPrintf("CNAME: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_CNAME]);
    sceKernelPrintf("INTERFACE: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_INTERFACE]);
    sceKernelPrintf("RCPSET: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_RCPSET]);
    sceKernelPrintf("LOAD_FL: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_LOAD_FL]);
    sceKernelPrintf("SYSMSG: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_SYSMSG]);
    sceKernelPrintf("PACKSET: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_PACKSET]);
#endif
}
