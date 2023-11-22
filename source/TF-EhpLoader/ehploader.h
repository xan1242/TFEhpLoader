#ifndef EHPLOADER_H
#define EHPLOADER_H

#define EHP_NAME_CNAME				"cname.ehp"
#define EHP_NAME_INTERFACE			"interface.ehp"
#define EHP_NAME_RCPSET				"rcpset.ehp"
#define EHP_NAME_LOAD_FL			"load_fl.ehp"
#define EHP_NAME_SYSMSG				"sysmsg.ehp"
#define EHP_NAME_PACKSET			"packset.ehp"

typedef enum _EhpType
{
    EHP_TYPE_UNK = -1,
    EHP_TYPE_CNAME,
    EHP_TYPE_INTERFACE,
    EHP_TYPE_RCPSET,
    EHP_TYPE_LOAD_FL,
    EHP_TYPE_SYSMSG,
    EHP_TYPE_PACKSET,
    EHP_TYPE_COUNT
}EhpType;

#define EHP_MAGIC 0x504845

// Forward-declare
void EhpLoaderInject(const char* folderPath);
#endif