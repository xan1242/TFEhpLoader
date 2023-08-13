#ifndef EHPLOADER_H
#define EHPLOADER_H

#define EHP_ADDR_CNAME_BIN			0x000FE9B0
#define EHP_ADDR_INTERFACE_BIN		0x000AEF90
#define EHP_ADDR_RCPSET_BIN			0x000D37E0
#define EHP_ADDR_LOAD_FL_BIN		0x000C8480
#define EHP_ADDR_SYSMSG_BIN			0x000FB0D0
#define EHP_ADDR_PACKSET_BIN		0x0010D580

#define EHP_NAME_CNAME				"cname.ehp"
#define EHP_NAME_INTERFACE			"interface.ehp"
#define EHP_NAME_RCPSET				"rcpset.ehp"
#define EHP_NAME_LOAD_FL			"load_fl.ehp"
#define EHP_NAME_SYSMSG				"sysmsg.ehp"
#define EHP_NAME_PACKSET			"packset.ehp"

typedef enum _EhpType
{
    EHP_TYPE_UNK,
    EHP_TYPE_CNAME,
    EHP_TYPE_INTERFACE,
    EHP_TYPE_RCPSET,
    EHP_TYPE_LOAD_FL,
    EHP_TYPE_SYSMSG,
    EHP_TYPE_PACKSET,
    EHP_TYPE_COUNT
}EhpType;

// Injection addresses
#define HOOK_ADDR_CREATE_CNAME			0x00031C78
#define HOOK_ADDR_CREATE_INTERFACE		0x00031C88
#define HOOK_ADDR_CREATE_RCPSET			0x00031C98
#define HOOK_ADDR_CREATE_LOAD_FL		0x00031CA8
#define HOOK_ADDR_CREATE_SYSMSG			0x00031CB8
#define HOOK_ADDR_CREATE_PACKSET		0x00031CC8

#define HOOK_ADDR_1_SEARCH_CNAME		0x00057368
#define HOOK_ADDR_1_SEARCH_INTERFACE	0x0002FACC
#define HOOK_ADDR_2_SEARCH_INTERFACE	0x000334C0
#define HOOK_ADDR_3_SEARCH_INTERFACE	0x0003E630
#define HOOK_ADDR_4_SEARCH_INTERFACE	0x00045A20
#define HOOK_ADDR_5_SEARCH_INTERFACE	0x0004AD44
#define HOOK_ADDR_1_SEARCH_RCPSET		0x00058374
#define HOOK_ADDR_2_SEARCH_RCPSET		0x000583B8
#define HOOK_ADDR_3_SEARCH_RCPSET		0x0005842C
#define HOOK_ADDR_1_SEARCH_LOAD_FL		0x00031D34
#define HOOK_ADDR_1_SEARCH_SYSMSG		0x00032BD0
#define HOOK_ADDR_1_SEARCH_PACKSET		0x0004C350

// Function addresses
#define FUNC_ADDR_CREATEFROMMEMORY		0x0001DF40
#define FUNC_ADDR_SEARCHFILE			0x0001E280

// Forward-declare
void EhpLoaderInject(const char* folderPath);
//void* ehploader_malloc(size_t size);
//void ehploader_free(void* data);
#endif