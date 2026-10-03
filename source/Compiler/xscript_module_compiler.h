#ifndef XSCRIPT_MODULE_COMPILER_H
#define XSCRIPT_MODULE_COMPILER_H
#pragma once
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"

namespace xscript_module_compiler
{
    enum state : std::uint8_t
    { OK
    , FAILURE
    };

    struct instance : xresource_pipeline::compiler::base
    {
        static std::unique_ptr<instance> Create(void);
    };
}

#endif
