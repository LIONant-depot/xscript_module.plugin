include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

# Fetch dependencies
FetchAndPopulate("https://github.com/LIONant-depot/xresource_pipeline_v2.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xstrtool.git" "main" )

DefineInterfaceComponent(xscript_module_compiler_ "dependencies"
  "source/Module/xscript_module_descriptor.h"
  "source/Module/xscript_module_export.h"
  "source/Module/xscript_module_files.h"
  "source/Module/xscript_module_paths.h"
)
