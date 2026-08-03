#include "Outrun.h"

/**
    The effect: the clip's own outlines as neon strokes, breaking away from the
    real geometry on demand.

    Read the note in SourcePlugin.cpp on why this file is listed directly in its
    own target rather than in `outrun_core`. The short version is that the
    `CFFGLPluginInfo` below is the one thing the two plugins must NOT share.

    The plugin ID differs from the source's, and it has to: Resolume keys a
    saved composition's effect to that ID, so two plugins sharing one would make
    a composition ambiguous about which of them it meant.
*/
namespace
{
class OutrunEffect : public outrun::OutrunPlugin
{
public:
	OutrunEffect() :
		OutrunPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< OutrunEffect >,                        // Create method
	"OU02",                                               // Plugin unique ID of maximum length 4
	"Outrun Trace",                                       // Plugin name
	2,                                                    // API major version number
	1,                                                    // API minor version number
	0,                                                    // Plugin major version number
	1,                                                    // Plugin minor version number
	FF_EFFECT,                                            // Plugin type
	"Traces the clip's outlines as breakaway neon",       // Plugin description
	"Outrun FFGL effect"                                  // About
);

extern "C" const char* OutrunEffectBuildStamp()
{
	return "outrun " OUTRUN_VERSION " effect, built " __DATE__ " " __TIME__;
}
