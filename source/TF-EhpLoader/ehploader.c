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
#include "../../includes/psp/minjector.h"
#include "../../includes/psp/patterns.h"
#include "ehploader.h"
#include "../../includes/psp/pspmallochelper.h"

// uncomment for debug logging via sceKernelPrintf
//#define EHPLOADER_DEBUG_PRINTS

void (*EhFolder_CreateFromMemory)(int unk, void* ehppointer) = (void (*)(int, void*))0x1DF40;
uintptr_t(*lEhFolder_SearchFile)(uintptr_t pEhFolder, char* filename) = (uintptr_t(*)(uintptr_t, char*))0x1E270;
uint32_t(*lEhFolder_GetFileSizeSub)(uintptr_t ptr, uintptr_t handle) = (uint32_t(*)(uintptr_t, uintptr_t))0x1E600;
void(*YgSys_Ms_GetDirName)(char* out) = (void(*)(char*))0x61548; // address in TF1 JP

int (*YgSys_strcmp)(const char*, const char*) = (int (*)(const char*, const char*))(0);
size_t(*YgSys_strlen)(const char* str) = (size_t(*)(const char*))(0);
char* (*YgSys_strcpy)(char* dst, const char* src) = (char* (*)(char*, const char*))(0);
char* (*YgSys_strcat)(char* dst, const char* src) = (char* (*)(char*, const char*))(0);

uintptr_t base_addr = 0;
char basePath[128];
char GameSerial[32];

void* ptrEhpFiles[EHP_TYPE_COUNT];
void* ptrEhpFilesOriginal[EHP_TYPE_COUNT];

char* tf_strstr(register char* string, char* substring)
{
    register char* a, * b;

    b = substring;
    if (*b == 0) {
        return string;
    }
    for (; *string != 0; string += 1) {
        if (*string != *b) {
            continue;
        }
        a = string;
        while (1) {
            if (*b == 0) {
                return string;
            }
            if (*a++ != *b++) {
                break;
            }
        }
        b = substring;
    }
    return NULL;
}

//
// Custom memory allocator - reuse the EhFolder space in EBOOT if at all possible!
//

size_t ehpSizes[EHP_TYPE_COUNT];
int ehpAllocSpaces[EHP_TYPE_COUNT];

size_t calculate_aligned_size(size_t size, size_t alignment) 
{
    size_t aligned_size = size + (alignment - (size % alignment)) % alignment;
    return aligned_size;
}

size_t ehploader_largest_malloc()
{
    size_t largest = 0;
    int largest_idx = 0;
    for (int i = 0; i < EHP_TYPE_COUNT; i++)
    {
        if (ehpAllocSpaces[i] > largest)
        {
            largest = ehpAllocSpaces[i];
            largest_idx = i;
        }
    }

    return largest;
}

void* ehploader_malloc(size_t size)
{
    size_t align_size = calculate_aligned_size(size, 4);
#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "alloc_size: 0x%X\taligned: 0x%X\n", size, align_size);
#endif

    size_t maxalloc = ehploader_largest_malloc();
    if (align_size > maxalloc)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "alloc too large to fit in executable space (max: 0x%X)! allocing normally...", maxalloc);
#endif
        return psp_malloc(size);
    }
    // find the most appropriate buffer

    // find the smallest buffer it can fit into
    int smallest = -1;
    int small_diff = INT32_MAX;
    for (int i = 0; i < EHP_TYPE_COUNT; i++)
    {
        int diff = ehpAllocSpaces[i] - align_size;
        if ((diff >= 0) && (diff < small_diff))
        {
            smallest = i;
            small_diff = diff;
        }
    }

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "smallest slot: %d\n", smallest, small_diff);
#endif

    uintptr_t result = (uintptr_t)ptrEhpFilesOriginal[smallest] + (ehpSizes[smallest] - ehpAllocSpaces[smallest]);
    ehpAllocSpaces[smallest] -= align_size;

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "remaining space in slot: 0x%X\n", ehpAllocSpaces[smallest]);
#endif

    return (void*)result;
}

void* LoadFileToMem(const char* path)
{
    SceIoStat st;
    int res = sceIoGetstat(path, &st);
    if (res < 0)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "sceIoGetstat fail! 0x%X\n", res);
#endif
        return NULL;
    }
    void* out = ehploader_malloc(st.st_size);

    if (out == NULL)
        return NULL;

    SceUID f = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (f < 0)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "sceIoOpen fail! 0x%X\n", f);
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
    sceKernelPrintf(MODULE_NAME ": " "EhFolder_CreateFromMemory (0x%X)", ptr);
#endif

    EhpType type = DetectType(ptr);

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "CreateFromMemory detected type: %d", type);
#endif
    if (type == EHP_TYPE_UNK)
        return EhFolder_CreateFromMemory(unk, ptr);

    void* ptrNewEhp = GetEhpPtr(type);
    if (ptrNewEhp != NULL)
        return EhFolder_CreateFromMemory(unk, ptrNewEhp);

    const char* filename = GetTypeFilename(type);

    size_t basePathLen = YgSys_strlen(basePath);
    size_t filenameLen = YgSys_strlen(filename);
    size_t totalLen = basePathLen + filenameLen + 1;
    char* completePath = (char*)psp_malloc(totalLen);
    if (completePath == NULL)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "String malloc failure!");
#endif
        return EhFolder_CreateFromMemory(unk, ptr);
    }
    YgSys_strcpy(completePath, basePath);
    YgSys_strcat(completePath, filename);

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "Loading: %s", completePath);
#endif
    void* memFile = LoadFileToMem(completePath);
    if (memFile == NULL)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "LoadFileToMem failure!");
#endif
        psp_free(completePath);
        return EhFolder_CreateFromMemory(unk, ptr);
    }

    ptrEhpFiles[type] = memFile;
    psp_free(completePath);

    EhFolder_CreateFromMemory(unk, memFile);

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "Load successful, addr: 0x%X", memFile);
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
    sceKernelPrintf(MODULE_NAME ": " "SearchFile: %s/%s", GetTypeFilename(type), filename);
#endif

    return EhFolder_SearchFile((uintptr_t)ptrNewEhp, filename, unk);
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

void EhpLoaderInject(const char* folderPath)
{
    // TODO: hook lSoftReset and reload this plugin on reset !!

    //sceKernelDelayThread(100000);
    base_addr = minj_GetBaseAddress();

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "Tag Force EhFolder Loader v%d.%d", MODULE_VERSION_MAJOR, MODULE_VERSION_MINOR);

    sceKernelPrintf(MODULE_NAME ": " "BaseAddr: 0x%X", base_addr);
    
    sceKernelPrintf(MODULE_NAME ": " "Searching functions");
#endif



    int bInTF6 = 0;
    int bUMDLoad = 0;
    
    uintptr_t ptr_lEhFolder_SearchFile = 0;
    uintptr_t ptr_lEhFolder_GetFileSizeSub = 0;
    uintptr_t ptr_EhFolder_CreateFromMemory = 0;
    uintptr_t ptr_EhFolder_SearchFile = 0;
    uintptr_t ptr_YgSys_InitApplication = 0;

    // detect game
    // we only care if it's TF6/Special or not because of the different compiler
    
    // try to find YgSys_Ms_GetDirPath and derive from that
    // get it from lYgSysDLFile_GetFileList via a universal pattern
    uintptr_t ptr_lYgSysDLFile_GetFileList_4998C = pattern.get_first("28 00 00 ? ? ? ? 60 01 06 ?", 7);

    // failsafe - no pattern = not in the right game
    if (ptr_lYgSysDLFile_GetFileList_4998C == 0)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "ERROR: Cannot detect Tag Force! Exiting...");
#endif
        return;
    }

    // find the first call after the memset within 6 instructions
    uintptr_t ptr_ptr_YgSys_Ms_GetDirPath = FindFirstJAL(ptr_lYgSysDLFile_GetFileList_4998C, 6);
    if (ptr_ptr_YgSys_Ms_GetDirPath == 0)
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "ERROR: Cannot detect Tag Force! Exiting...");
#endif
        return;
    }

    uintptr_t ptr_YgSys_Ms_GetDirPath = minj_GetBranchDestination(ptr_ptr_YgSys_Ms_GetDirPath);
    uintptr_t ptr_YgSys_Ms_GetDirName = minj_GetBranchDestination((ptr_YgSys_Ms_GetDirPath + 0x1C));

    YgSys_Ms_GetDirName = (void (*)(char*))(ptr_YgSys_Ms_GetDirName);

    YgSys_Ms_GetDirName(GameSerial);
    GameSerial[9] = '\0';

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "Detected game: %s", GameSerial);
#endif

    if ((tf_strstr(GameSerial, "ULJM05940")) || (tf_strstr(GameSerial, "NPJH00142")))
    {
        bInTF6 = 1;
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "Using TF6/Special mode!");
#endif
    }
    // find stdlib functions in the exe first!
    if (bInTF6)
    {
        uintptr_t funcPtr = pattern.get_first("00 00 86 80 08 00 C0 50 00 00 82 90 00 00 A7 80", 0);
        YgSys_strcmp = (int (*)(const char*, const char*))(funcPtr);

        funcPtr = pattern.get_first("00 00 86 80 05 00 C0 10 25 28 80 00 01 00 84 24", 0);
        YgSys_strlen = (size_t(*)(const char*))(funcPtr);

        // funcPtr = minj_GetBranchDestination(ptr_YgSys_Ms_GetDirName + 0x14);
        // YgSys_strcpy = (char* (*)(char*, const char*))(funcPtr);
        // 
        // funcPtr = minj_GetBranchDestination(ptr_YgSys_Ms_GetDirName + 0x24);
        // YgSys_strcat = (char* (*)(char*, const char*))(funcPtr);
    }
    else
    {
        uintptr_t ptr_wctomb_r_12500 = pattern.get_first("00 00 B0 AF 10 00 BF AF ? ? ? ? ? ? ? ? 02 00 42 2C", 0) + 8;
        uintptr_t funcPtr = minj_GetBranchDestination(ptr_wctomb_r_12500);
        YgSys_strlen = (size_t(*)(const char*))(funcPtr);

        uintptr_t ptr_lEhScript_ThreadMain_38D50 = pattern.get_first("58 00 03 AE 04 00 04 26 ? ? ? ? ? ? A5 24", 0) + 8;
        funcPtr = minj_GetBranchDestination(ptr_lEhScript_ThreadMain_38D50);
        YgSys_strcmp = (int (*)(const char*, const char*))(funcPtr);

        // uintptr_t ptr_lEhScript_ThreadMain_18834 = pattern.get_first("21 40 00 00 24 00 04 26 ? ? ? ? 04 00 05 26", 0) + 8;
        // funcPtr = minj_GetBranchDestination(ptr_lEhScript_ThreadMain_18834);
        // YgSys_strcpy = (char* (*)(char*, const char*))(funcPtr);
    }

    YgSys_strcpy = (char* (*)(char*, const char*))(minj_GetBranchDestination(ptr_YgSys_Ms_GetDirPath + 0x14));
    YgSys_strcat = (char* (*)(char*, const char*))(minj_GetBranchDestination(ptr_YgSys_Ms_GetDirPath + 0x28));

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "YgSys_strcmp: 0x%X", YgSys_strcmp);
    sceKernelPrintf(MODULE_NAME ": " "YgSys_strlen: 0x%X", YgSys_strlen);
    sceKernelPrintf(MODULE_NAME ": " "YgSys_strcpy: 0x%X", YgSys_strcpy);
    sceKernelPrintf(MODULE_NAME ": " "YgSys_strcat: 0x%X", YgSys_strcat);
#endif

    YgSys_strcpy(basePath, folderPath);

    char* flagFilePath = (char*)psp_malloc(YgSys_strlen(basePath) + 1 + sizeof(EHP_UMDLOAD_FLAGFILENAME));
    YgSys_strcpy(flagFilePath, basePath);
    YgSys_strcat(flagFilePath, EHP_UMDLOAD_FLAGFILENAME);
    SceUID f = sceIoOpen(flagFilePath, PSP_O_RDONLY, 0);
    if (f < 0)
        bUMDLoad = 0;
    else
    {
#ifdef EHPLOADER_DEBUG_PRINTS
        sceKernelPrintf(MODULE_NAME ": " "UMDLOAD enabled!");
#endif
        bUMDLoad = 1;
        sceIoClose(f);
    }
    psp_free(flagFilePath);


    if (bUMDLoad)
    {
        YgSys_strcpy(basePath, "disc0:/PSP_GAME/USRDIR/" EHP_SUBFOLDER_NAME "/");
    }
    else
    {
        YgSys_strcat(basePath, EHP_SUBFOLDER_NAME "/");
        YgSys_strcat(basePath, GameSerial);
        YgSys_strcat(basePath, "/");
    }

#ifdef EHPLOADER_DEBUG_PRINTS
    //sceKernelPrintf(MODULE_NAME ": " "Detected game: %s", GameSerial);
    sceKernelPrintf(MODULE_NAME ": " "BasePath: %s", basePath);
#endif

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
    ptr_EhFolder_CreateFromMemory = minj_GetBranchDestination((FindFirstJAL(ptr_YgSys_InitApplication, 10)));


#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "lEhFolder_SearchFile: 0x%X", ptr_lEhFolder_SearchFile);
    sceKernelPrintf(MODULE_NAME ": " "lEhFolder_GetFileSizeSub: 0x%X", ptr_lEhFolder_GetFileSizeSub);
    sceKernelPrintf(MODULE_NAME ": " "EhFolder_CreateFromMemory: 0x%X", ptr_EhFolder_CreateFromMemory);
    sceKernelPrintf(MODULE_NAME ": " "EhFolder_SearchFile: 0x%X", ptr_EhFolder_SearchFile);
    sceKernelPrintf(MODULE_NAME ": " "YgSys_InitApplication: 0x%X", ptr_YgSys_InitApplication);

    sceKernelPrintf(MODULE_NAME ": " "Function search done");
#endif

    // update func addresses
    EhFolder_CreateFromMemory = (void (*)(int, void*))(ptr_EhFolder_CreateFromMemory);
    lEhFolder_SearchFile = (uintptr_t(*)(uintptr_t, char*))(ptr_lEhFolder_SearchFile );
    lEhFolder_GetFileSizeSub = (uint32_t(*)(uintptr_t, uintptr_t))(ptr_lEhFolder_GetFileSizeSub);

    // replace the original EhFolder_SearchFile
    minj_MakeJMPwNOP(ptr_EhFolder_SearchFile, (uintptr_t)&EhFolder_SearchFile_Hook);
    
    uint32_t numEHP = 1;
    uintptr_t ptrHookStart = 0;

    // find the first JAL within the first 10 instructions
    ptrHookStart = FindFirstJAL(ptr_YgSys_InitApplication, 10);

    if (ptrHookStart == 0)
        return;

    // cname
    minj_MakeCALL(ptrHookStart, (uintptr_t)&EhFolder_CreateFromMemory_Hook);

    // hook the rest of the calls at the beginning of the function
    uintptr_t ptrJalHook = ptrHookStart + 4;
    do
    {
        // find the JAL within next 5 instructions
        ptrJalHook = FindFirstJAL(ptrJalHook, 5);

        if (ptrJalHook)
        {
            minj_MakeCALL(ptrJalHook, (uintptr_t)&EhFolder_CreateFromMemory_Hook);
#ifdef EHPLOADER_DEBUG_PRINTS
            sceKernelPrintf(MODULE_NAME ": " "Hooking EhFolder_CreateFromMemory: 0x%X", ptrJalHook);
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
        uintptr_t ptrEHP = minj_DiscoverPtr(ptrDiscoverStart, NULL, &nextStart, MIPSR_a1);

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

        if (tf_strstr(EHPFirstFileName, "sysmsg"))
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

    for (int i = 0; i < EHP_TYPE_COUNT; i++)
    {
        if (ptrEhpFilesOriginal[i] != NULL)
        {
            ehpSizes[i] = *(uint32_t*)((uintptr_t)ptrEhpFilesOriginal[i] + sizeof(uint32_t));
            ehpAllocSpaces[i] = *(uint32_t*)((uintptr_t)ptrEhpFilesOriginal[i] + sizeof(uint32_t));
        }
    }

#ifdef EHPLOADER_DEBUG_PRINTS
    sceKernelPrintf(MODULE_NAME ": " "===EhFolder ptrs:===");
    sceKernelPrintf(MODULE_NAME ": " "CNAME: 0x%X\tsize: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_CNAME], ehpSizes[EHP_TYPE_CNAME]);
    sceKernelPrintf(MODULE_NAME ": " "INTERFACE: 0x%X\tsize: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_INTERFACE], ehpSizes[EHP_TYPE_INTERFACE]);
    sceKernelPrintf(MODULE_NAME ": " "RCPSET: 0x%X\tsize: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_RCPSET], ehpSizes[EHP_TYPE_RCPSET]);
    sceKernelPrintf(MODULE_NAME ": " "LOAD_FL: 0x%X\tsize: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_LOAD_FL], ehpSizes[EHP_TYPE_LOAD_FL]);
    sceKernelPrintf(MODULE_NAME ": " "SYSMSG: 0x%X\tsize: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_SYSMSG], ehpSizes[EHP_TYPE_SYSMSG]);
    sceKernelPrintf(MODULE_NAME ": " "PACKSET: 0x%X\tsize: 0x%X", ptrEhpFilesOriginal[EHP_TYPE_PACKSET], ehpSizes[EHP_TYPE_PACKSET]);
#endif
}
