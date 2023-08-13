//
// Tag Force 3 Memory EHP loader
// Loads EHPs into memory and overrides embedded ones in the executable
// 
// by xan1242 / Tenjoin 
// 
// Entrypoint code
// 

#include <pspsdk.h>
#include <pspuser.h>
#include <pspctrl.h>
#include <systemctrl.h>
#include <kubridge.h>
#include <stdio.h>
#include <string.h>

#include "../../includes/psp/injector.h"

#include "ehploader.h"
#include "../../includes/psp/pspmallochelper.h"

#define MODULE_NAME_INTERNAL "modehsys"
#define MODULE_NAME "TF3-EhpLoader"

#define MODULE_VERSION_MAJOR 1
#define MODULE_VERSION_MINOR 0

// Uncomment for logging
// We use a global definition like so to reduce final binary size (which is very important because PSP is memory constrained!)
//#define LOG

// We ignore Intellisense here to reduce squiggles in VS
#ifndef __INTELLISENSE__
PSP_MODULE_INFO(MODULE_NAME, 0, MODULE_VERSION_MAJOR, MODULE_VERSION_MINOR);
#endif

// Forward-declare initialization function
int MainInit(const char* basePath);

int bPPSSPP = 0;
static STMOD_HANDLER previous;

#define EHP_SUBFOLDER_NAME "ehps"
char base_path[128];

#ifdef LOG
#define LOG_NAME MODULE_NAME ".log"
// Default initialized path
char logpath[128] = "ms0:/seplugins/" LOG_NAME;

//
// A basic printf logger that writes to a file.
//
int logPrintf(const char* text, ...) {
    va_list list;
    char string[512];

    va_start(list, text);
    vsprintf(string, text, list);
    va_end(list);

    SceUID fd = sceIoOpen(logpath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, string, strlen(string));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }

    return 0;
}
#endif

void CheckModules()
{
    SceUID modules[10];
    int count = 0;
    int bFoundMainModule = 0;
    int bFoundInternalModule = 0;
    if (sceKernelGetModuleIdList(modules, sizeof(modules), &count) >= 0)
    {
        int i;
        SceKernelModuleInfo info;
        for (i = 0; i < count; ++i)
        {
            info.size = sizeof(SceKernelModuleInfo);
            if (sceKernelQueryModuleInfo(modules[i], &info) < 0)
            {
                continue;
        }
            if (strcmp(info.name, MODULE_NAME_INTERNAL) == 0)
            {
#ifdef LOG
                logPrintf("Found module " MODULE_NAME_INTERNAL);
                logPrintf("text_addr: 0x%X\ntext_size: 0x%X", info.text_addr, info.text_size);
#endif
                injector.SetGameBaseAddress(info.text_addr, info.text_size);

                bFoundMainModule = 1;
            }
            else if (strcmp(info.name, MODULE_NAME) == 0)
            {
#ifdef LOG
                logPrintf("PRX module " MODULE_NAME);
                logPrintf("text_addr: 0x%X\ntext_size: 0x%X", info.text_addr, info.text_addr);
#endif
                injector.SetModuleBaseAddress(info.text_addr, info.text_size);

                bFoundInternalModule = 1;
            }
            }
    }

    if (bFoundInternalModule)
    {
        if (bFoundMainModule)
        {
            MainInit(base_path);
        }
    }

    return;
}

void CheckModulesPSP()
{
    SceModule2 mod = { 0 };
    int kuErrCode = kuKernelFindModuleByName(MODULE_NAME_INTERNAL, &mod);
    if (kuErrCode != 0)
        return;

    SceModule2 this_module = { 0 };
    kuErrCode = kuKernelFindModuleByName(MODULE_NAME, &this_module);
    if (kuErrCode != 0)
        return;

    injector.SetGameBaseAddress(mod.text_addr, mod.text_size);
    injector.SetModuleBaseAddress(this_module.text_addr, this_module.text_addr);

    MainInit(base_path);
}

//int OnModuleStart(SceModule2* mod) 
//{
//    if (!previous)
//        return 0;
//    
//    return previous(mod);
//}

void SetDefaultPaths()
{
    if (bPPSSPP) 
    {
        strcpy(base_path, "ms0:/PSP/PLUGINS/" MODULE_NAME "/");
#ifdef LOG
        strcpy(logpath, "ms0:/PSP/PLUGINS/" MODULE_NAME "/" LOG_NAME);
#endif
    }
    else 
    { 
        strcpy(base_path, "ms0:/seplugins/");
#ifdef LOG
        strcpy(logpath, "ms0:/seplugins/" LOG_NAME);
#endif
    }
}

int module_start(SceSize argc, void* argp) 
{
    char* ptr_path;
    // If a kemulator interface exists, we know that we're in an emulator
    if (sceIoDevctl("kemulator:", 0x00000003, NULL, 0, NULL, 0) == 0) 
        bPPSSPP = 1;

    if (argc > 0) 
    { 
        // on real hardware we use module_start's argp path
        // location depending on where prx is loaded from
        strcpy(base_path, (char*)argp);
        ptr_path = strrchr(base_path, '/');
        if (ptr_path)
            *(ptr_path + 1) = '\0';
        else
            SetDefaultPaths();
#ifdef LOG
        strcpy(logpath, (char*)argp);
        ptr_path = strrchr(logpath, '/');
        if (ptr_path)
            strcpy(ptr_path + 1, LOG_NAME);
        else
            SetDefaultPaths();
#endif
    }
    else 
    { 
        // no arguments found
        SetDefaultPaths();
    }

    if (bPPSSPP)
        CheckModules(); // scan the modules using normal/official syscalls (https://github.com/hrydgard/ppsspp/pull/13335#issuecomment-689026242)
    else // PSP
    {
        CheckModulesPSP();
        //previous = sctrlHENSetStartModuleHandler(OnModuleStart);
    }
    return 0;
}

//
// MainInit
//
int MainInit(const char* basePath) {
#ifdef LOG
    logPrintf(MODULE_NAME " MainInit");
#endif

    size_t basePathLen = strlen(basePath);
    size_t foldernameLen = sizeof(EHP_SUBFOLDER_NAME);
    size_t totalLen = basePathLen + foldernameLen + 2;
    char* completePath = (char*)psp_malloc(totalLen);
    if (completePath == NULL)
    {
        sceKernelDcacheWritebackAll();
        return 0;
    }
    strcpy(completePath, basePath);
    strcat(completePath, EHP_SUBFOLDER_NAME "/");

    EhpLoaderInject(completePath);

    psp_free(completePath);

    sceKernelDcacheWritebackAll();
    
    return 0;
}
