#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace outrun
{
namespace
{
inline float clamp01( float value )
{
	return std::min( std::max( value, 0.0f ), 1.0f );
}

inline float lerp( float from, float to, float t )
{
	return from + ( to - from ) * clamp01( t );
}

/// Geometric interpolation. Equal slider movements are equal *ratios*, which
/// is the right behaviour for any quantity where the question is "how many
/// times more" rather than "how much more".
inline float geometric( float from, float to, float t )
{
	return from * std::pow( to / from, clamp01( t ) );
}
} // namespace

float SensitivityFromParam( float value )
{
	return geometric( 0.01f, 1.0f, value );
}

float SoftnessFromParam( float value )
{
	return lerp( 0.05f, 1.0f, value );
}

float DetailFromParam( float value )
{
	return lerp( 0.0f, 4.0f, value );
}

float AttackFromParam( float value )
{
	//Stays fast across the whole range. Stability is allowed to buy
	//persistence but is not allowed to buy lag: an outline that arrives late
	//is a worse artefact than the flicker being traded away, because it is
	//attached to something the eye is already tracking.
	return lerp( 1.0f, 0.75f, value );
}

float ReleaseFromParam( float value )
{
	//1.0 is no persistence at all. 0.02 is a time constant of fifty frames,
	//which is a little under a second at 60fps and about as long as a stroke
	//can hang on after its edge has gone before it reads as a ghost.
	return geometric( 1.0f, 0.02f, value );
}

float WidthFromParam( float value )
{
	return geometric( 1.0f, 32.0f, value );
}

float PathScaleFromParam( float value )
{
	return lerp( 0.2f, 1.5f, value );
}

float PathDetailFromParam( float value )
{
	return geometric( 1.0f, 16.0f, value );
}

float HorizonFromParam( float value )
{
	return lerp( 0.2f, 0.8f, value );
}

float TraceAngleFromParam( float value )
{
	return clamp01( value );
}

float BreakHueFromParam( float value )
{
	return lerp( 0.0f, 0.5f, value );
}

float SpeedFromParam( float value )
{
	//The dead zone makes zero an actual place: below it the clock is stopped,
	//not merely slow, so a cued look can be parked without hunting for the one
	//slider pixel that means "off".
	if( value <= 0.02f )
		return 0.0f;

	return geometric( 0.02f, 2.0f, ( value - 0.02f ) / 0.98f );
}

float SpreadFromParam( float value )
{
	return geometric( 0.25f, 8.0f, value );
}

float SaturationFromParam( float value )
{
	return lerp( 0.0f, 1.5f, value );
}

float BrightnessFromParam( float value )
{
	return lerp( 0.0f, 2.0f, value );
}

float GlowFromParam( float value )
{
	return lerp( 0.0f, 3.0f, value );
}

float GlowSizeFromParam( float value )
{
	return geometric( 0.5f, 8.0f, value );
}

} // namespace outrun
