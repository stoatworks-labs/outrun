#include "Outrun.h"

/**
    The one registration. Both engines live inside this single effect plugin;
    the Engine dropdown chooses between them.

    This file is listed directly in the Outrun MODULE target, not in
    outrun_core: `CFFGLPluginInfo` registers itself from a file-scope
    constructor and nothing ever references it by name, so in a STATIC archive
    the linker is entitled to drop the whole translation unit -- giving a
    bundle that loads, exports `plugMain`, and reports that it contains no
    plugins. The core stays an OBJECT library for the same reason.

        nm -gU Outrun.bundle/Contents/MacOS/Outrun | grep plugMain
*/
namespace
{
class OutrunEffect : public outrun::OutrunPlugin
{
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< OutrunEffect >,                        // Create method
	"OU01",                                               // Plugin unique ID of maximum length 4
	"Outrun",                                             // Plugin name
	2,                                                    // API major version number
	1,                                                    // API minor version number
	0,                                                    // Plugin major version number
	1,                                                    // Plugin minor version number
	FF_EFFECT,                                            // Plugin type
	"Neon synthwave strokes: trace the clip's outlines or generate paths",
	"Outrun FFGL effect"                                  // About
);

extern "C" const char* OutrunBuildStamp()
{
	return "outrun " OUTRUN_VERSION ", built " __DATE__ " " __TIME__;
}
