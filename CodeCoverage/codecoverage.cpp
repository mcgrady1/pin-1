#include "pin.H"

#include <jansson.h>
#include <map>
#include <string>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>
#include <list>


typedef std::map<std::string, std::pair<ADDRINT, ADDRINT> > MODULE_BLACKLIST_T;
typedef MODULE_BLACKLIST_T MODULE_LIST_T;
typedef std::map<ADDRINT, UINT32> BASIC_BLOCKS_INFO_T;

UINT64 instruction_counter = 0;
UINT64 thread_counter = 0;
BASIC_BLOCKS_INFO_T basic_blocks_info;
MODULE_LIST_T module_list;
char *process_name; 


KNOB<std::string> KnobOutputPath(
    KNOB_MODE_WRITEONCE,
    "pintool",
    "o",
    "./log/test-log.json",
    "Specify where you want to store the JSON report"
);

KNOB<std::string> KnobTimeoutMs(
    KNOB_MODE_WRITEONCE,
    "pintool",
    "r",
    "infinite",
    "Set a timeout for the instrumentation"
);


bool is_main_module(ADDRINT address)
{
    for(MODULE_LIST_T::const_iterator it = module_list.begin(); it != module_list.end(); ++it)
    {
        ADDRINT low_address = it->second.first, high_address = it->second.second;
        if(address >= low_address && address <= high_address)
            return true;
    }

    return false;
}

INT32 Usage()
{
    std::cerr << "This pintool allows you to generate a JSON report that will contain the address of each basic block executed." << std::endl << std::endl;
    std::cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

VOID PIN_FAST_ANALYSIS_CALL handle_basic_block(UINT32 number_instruction_in_bb, ADDRINT address_bb)
{
//  LOG("[ANALYSIS] BBL Address: " + hexstr(address_bb) + "\n");
    basic_blocks_info[address_bb] = number_instruction_in_bb;
    instruction_counter += number_instruction_in_bb;
}

VOID trace_instrumentation(TRACE trace, VOID *v)
{
    if(!is_main_module(TRACE_Address(trace)))
        return;

    for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // LOG("[INSTRU] BBL Address: " + hexstr(BBL_Address(bbl)) + ", " + hexstr(BBL_NumIns(bbl)) + "\n");
        
        BBL_InsertCall(
            bbl,
            IPOINT_ANYWHERE,
            (AFUNPTR)handle_basic_block,
            IARG_FAST_ANALYSIS_CALL,
            IARG_UINT32,
            BBL_NumIns(bbl),

            IARG_ADDRINT,
            BBL_Address(bbl),

            IARG_END
        );
    }
}

VOID image_instrumentation(IMG img, VOID * v)
{
    ADDRINT module_low_limit = IMG_LowAddress(img), module_high_limit = IMG_HighAddress(img); 
    const std::string image_path = IMG_Name(img);

    std::pair<std::string, std::pair<ADDRINT, ADDRINT> > module_info = std::make_pair(
        image_path,
        std::make_pair(
            module_low_limit,
            module_high_limit
        )
    );

    module_list.insert(module_info);
}

VOID save_instrumentation_infos()
{
    json_t *bbls_info = json_object();
    json_t *bbls_list = json_array();
    json_t *bbl_info = json_object();

    json_object_set_new(bbls_info, "unique_count", json_integer(basic_blocks_info.size()));
    json_object_set_new(bbls_info, "list", bbls_list);

    for(BASIC_BLOCKS_INFO_T::const_iterator it = basic_blocks_info.begin(); it != basic_blocks_info.end(); ++it)
    {
        bbl_info = json_object();
        json_object_set_new(bbl_info, "address", json_string(hexstr(it->first).c_str()));
        json_object_set_new(bbl_info, "nbins", json_integer(it->second));
        json_array_append_new(bbls_list, bbl_info);
    }

    json_t *modules = json_object();
    json_t *modules_list_ = json_array();

    json_object_set_new(modules, "unique_count", json_integer(module_list.size()));
    json_object_set_new(modules, "list", modules_list_);

    for(MODULE_BLACKLIST_T::const_iterator it = module_list.begin(); it != module_list.end(); ++it)
    {
        json_t *mod_info = json_object();
        json_object_set_new(mod_info, "path", json_string(it->first.c_str()));
        json_object_set_new(mod_info, "low_address", json_string(hexstr(it->second.first).c_str()));
        json_object_set_new(mod_info, "high_address", json_string(hexstr(it->second.second).c_str()));
        json_array_append_new(modules_list_, mod_info);
    }

    json_t *root = json_object();
    json_object_set_new(root, "basic_blocks_info", bbls_info);
    json_object_set_new(root, "modules", modules);

    FILE* f = fopen(KnobOutputPath.Value().c_str(), "w");
    json_dumpf(root, f, JSON_COMPACT | JSON_ENSURE_ASCII);
    fclose(f);
}

VOID pin_is_detached(VOID *v)
{
    save_instrumentation_infos();
    PIN_ExitProcess(0);
}

VOID this_is_the_end(INT32 code, VOID *v)
{
    save_instrumentation_infos();
}

VOID sleeping_thread(VOID* v)
{
    if(KnobTimeoutMs.Value() == "infinite")
        return;

    PIN_Sleep(atoi(KnobTimeoutMs.Value().c_str()));
    PIN_Detach();
}

int main(int argc, char *argv[])
{
    if(PIN_Init(argc,argv))
        return Usage();

    TRACE_AddInstrumentFunction(trace_instrumentation, 0);
    PIN_AddFiniFunction(this_is_the_end, 0);
    IMG_AddInstrumentFunction(image_instrumentation, 0);
    PIN_AddDetachFunction(pin_is_detached, 0);

    PIN_SpawnInternalThread(
        sleeping_thread,
        0,
        0,
        NULL
    );

    PIN_StartProgram();
    
    return 0;
}
