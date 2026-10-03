#include "xscript_module_compiler.h"
#include <string>

int main(int argc, const char* argv[])
{
    auto Compiler = xscript_module_compiler::instance::Create();

    if (auto Err = Compiler->Parse(argc, argv); Err)
    {
        Err.ForEachInChain([&](xerr Error)
        {
            xstrtool::print("ERROR: {}\n", Error.getMessage());
            if (auto Hint = Error.getHint(); Hint.empty() == false)
                xstrtool::print("- HINT: {}\n", Hint);
        });
        return 1;
    }

    if (auto Err = Compiler->Compile(); Err)
    {
        xstrtool::print("{}\nERROR: Fail to compile(2)\n", Err.getMessage());
        if (auto Hint = Err.getHint(); Hint.empty() == false)
            xstrtool::print("- HINT: {}\n", Hint);
        return Err.getStateUID();
    }

    return 0;
}
