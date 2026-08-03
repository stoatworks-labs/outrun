#include "Outrun.h"

/**
    The generator: neon paths over its own background, no input.

    **This file is listed directly in the OutrunSource target, not in
    outrun_core.** Both plugins share the class; what they do not share is the
    `CFFGLPluginInfo` below, and putting either registration in the shared
    library would register both plugins into both bundles.

    It is also why the shared library is an OBJECT library rather than a STATIC
    one. `CFFGLPluginInfo` registers itself from a file-scope constructor and
    nothing ever references it by name, so in an archive the linker is entitled
    to drop the whole translation unit -- giving a bundle that loads, exports
    `plugMain`, and reports that it contains no plugins.

        nm -gU Outrun.bundle/Contents/MacOS/Outrun | grep plugMain
*/
namespace
{
class OutrunSource : public outrun::OutrunPlugin
{
public:
	OutrunSource() :
		OutrunPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< OutrunSource >,                        // Create method
	"OU01",                                               // Plugin unique ID of maximum length 4
	"Outrun",                                             // Plugin name
	2,                                                    // API major version number
	1,                                                    // API minor version number
	0,                                                    // Plugin major version number
	1,                                                    // Plugin minor version number
	FF_SOURCE,                                            // Plugin type
	"Neon synthwave paths, grids and waveforms",          // Plugin description
	"Outrun FFGL source"                                  // About
);

extern "C" const char* OutrunSourceBuildStamp()
{
	return "outrun " OUTRUN_VERSION " source, built " __DATE__ " " __TIME__;
}
