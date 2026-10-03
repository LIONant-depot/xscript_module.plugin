// The compiler of the ScriptModule resource: Descriptor.txt in, the module's CMake file out.
//
// The compiled resource IS the CMake file (Cache/Resources/Platforms/WINDOWS/ScriptModule/xx/yy/<guid>): the same text the module editor's "Export CMake" writes
// (xscript_module_export.h), so there is one way to turn a descriptor into CMake. The project's Game resource includes these files to make the game's project.
//
// What this compile depends on is the descriptor and nothing else: it lists NO dependency in dependencies.txt. The files of source_db are not inputs of it - what the
// descriptor LISTS is the input, and that is in Descriptor.txt - so the edit of a .h or a .cpp never queues this compile and never makes the CMake be written again.
// (A listed file that is not on disk is only a warning here: the compile cannot know when the file comes back, and the build of Game.dll says it loudly.)
#include "dependencies/xstrtool/source/xstrtool.h"
#include "plugins/xscript_module.plugin/source/Module/xscript_module_export.h"
#include "source/Compiler/xscript_module_compiler.h"

#include "dependencies/xproperty/source/xcore/my_properties.cpp"

namespace xscript_module_compiler
{
    struct implementation final : xscript_module_compiler::instance
    {
        xerr onCompile(void) override
        {
            const auto Start = std::filesystem::file_time_type::clock::now();           // before the descriptor is read: see StampCompileOutput
            const std::filesystem::path DescFolder = std::filesystem::path(m_ProjectPaths.m_Project) / m_InputSrcDescriptorPath;

            //
            // Load and validate the descriptor
            //
            displayProgressBar("Reading the descriptor", 0.0f);
            xscript::module::descriptor Descriptor;
            {
                std::string Error;
                if (!xscript::module::Read(DescFolder, Descriptor, &Error))
                {
                    LogMessage(xresource_pipeline::msg_type::ERROR, std::format("The descriptor cannot be read: {}", Error));
                    return xerr::create_f<state, "ScriptModule: Descriptor.txt cannot be read">();
                }
                std::vector<std::string> Errors;
                Descriptor.Validate(Errors);
                if (!Errors.empty())
                {
                    for (const auto& E : Errors) LogMessage(xresource_pipeline::msg_type::ERROR, E);
                    return xerr::create_f<state, "ScriptModule failed validation">();
                }
            }

            //
            // The name of the module is what the user sees: it names the folder in Visual Studio and the CMake function of the module
            //
            std::string Name = "Module";
            {
                xresource_pipeline::info Info;
                xproperty::settings::context Context;
                if (auto Err = Info.Serialize(true, (DescFolder / L"info.txt").wstring(), Context); !Err && !Info.m_Name.empty()) Name = Info.m_Name;
            }

            //
            // The files that are listed and not there are said, once, here: the build is where they hurt
            //
            for (const auto& Missing : xscript::module::ResolveDescriptor(Descriptor, DescFolder).m_Missing)
                LogMessage(xresource_pipeline::msg_type::WARNING, std::format("The file '{}' is listed in the module but it is not in source_db: it is left out of the build", Missing));

            //
            // Write the CMake file of the module
            //
            displayProgressBar("Writing the CMake file", 0.5f);
            for (auto& T : m_Target)
            {
                if (!T.m_bValid) continue;
                const std::filesystem::path File = T.m_DataPath;
                if (const auto Why = xscript::module::WriteExportCMake(Descriptor, DescFolder, Name, File); !Why.empty())
                {
                    LogMessage(xresource_pipeline::msg_type::ERROR, Why);
                    return xerr::create_f<state, "ScriptModule: the CMake file could not be written">();
                }
                xscript::module::StampCompileOutput(File, Start);
            }
            displayProgressBar("Writing the CMake file", 1.0f);
            return {};
        }
    };
}

std::unique_ptr<xscript_module_compiler::instance> xscript_module_compiler::instance::Create(void)
{
    return std::make_unique<implementation>();
}
