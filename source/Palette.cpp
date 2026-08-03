#include "Palette.h"

#include <algorithm>
#include <cmath>

namespace outrun
{
namespace
{
struct Stop
{
	float position;
	float r, g, b;
};

/**
    The palettes.

    Two rules were followed throughout, and both are worth knowing before
    editing one.

    **A palette must not go to black unless black is the point.** These paint
    lit tubes: a black stop is a length of tube that has gone out, and reads
    as a fault rather than a colour. Neon Noir is the one that gets *near*
    black, because noir is the point -- but even its floor stays a visible
    midnight blue.

    **Every palette wraps.** The stroke coordinate walks the palette round, so
    the last stop returns to the first unless a visible seam is wanted --
    Cyberdeck's hard thirds are the deliberate exception, its seams being
    exactly as hard as its interior boundaries.
*/
const Stop kMiami[] = {
	{ 0.000f, 1.00f, 0.10f, 0.60f },
	{ 0.350f, 0.65f, 0.15f, 0.95f },
	{ 0.650f, 0.10f, 0.75f, 1.00f },
	{ 0.850f, 0.20f, 0.95f, 0.95f },
	{ 1.000f, 1.00f, 0.10f, 0.60f },
};

//Magenta sinking into ultramarine and coming back: the album-cover gradient.
const Stop kOutrun[] = {
	{ 0.000f, 1.00f, 0.05f, 0.55f },
	{ 0.300f, 0.60f, 0.08f, 0.80f },
	{ 0.550f, 0.15f, 0.10f, 0.70f },
	{ 0.750f, 0.45f, 0.06f, 0.75f },
	{ 1.000f, 1.00f, 0.05f, 0.55f },
};

const Stop kSunsetDrive[] = {
	{ 0.000f, 0.35f, 0.05f, 0.55f },
	{ 0.300f, 0.85f, 0.20f, 0.45f },
	{ 0.550f, 1.00f, 0.45f, 0.15f },
	{ 0.800f, 1.00f, 0.30f, 0.45f },
	{ 1.000f, 0.35f, 0.05f, 0.55f },
};

const Stop kLaser[] = {
	{ 0.000f, 1.00f, 0.00f, 0.80f },
	{ 0.450f, 0.60f, 0.20f, 1.00f },
	{ 0.800f, 0.95f, 0.88f, 1.00f },
	{ 1.000f, 1.00f, 0.00f, 0.80f },
};

//The 1984 pastel pairing: teal and pink and nothing else.
const Stop kVice[] = {
	{ 0.000f, 0.05f, 0.95f, 0.85f },
	{ 0.500f, 1.00f, 0.35f, 0.75f },
	{ 1.000f, 0.05f, 0.95f, 0.85f },
};

const Stop kChrome[] = {
	{ 0.000f, 0.25f, 0.35f, 0.55f },
	{ 0.400f, 0.85f, 0.92f, 1.00f },
	{ 0.550f, 1.00f, 1.00f, 1.00f },
	{ 0.750f, 0.55f, 0.75f, 0.95f },
	{ 1.000f, 0.25f, 0.35f, 0.55f },
};

const Stop kUltraviolet[] = {
	{ 0.000f, 0.32f, 0.00f, 0.68f },
	{ 0.500f, 0.25f, 0.30f, 1.00f },
	{ 1.000f, 0.32f, 0.00f, 0.68f },
};

//Cyan flaring out of midnight blue. The floor is dark on purpose but never
//black -- see the file comment.
const Stop kNeonNoir[] = {
	{ 0.000f, 0.05f, 0.10f, 0.30f },
	{ 0.450f, 0.05f, 0.35f, 0.60f },
	{ 0.700f, 0.10f, 0.95f, 1.00f },
	{ 0.850f, 0.60f, 1.00f, 1.00f },
	{ 1.000f, 0.05f, 0.10f, 0.30f },
};

const Stop kAcid[] = {
	{ 0.000f, 0.10f, 0.90f, 0.15f },
	{ 0.500f, 0.75f, 1.00f, 0.10f },
	{ 1.000f, 0.10f, 0.90f, 0.15f },
};

//Low-pressure sodium streetlight through tungsten: the palette for neon that
//should read as city rather than as club.
const Stop kSodium[] = {
	{ 0.000f, 1.00f, 0.45f, 0.00f },
	{ 0.500f, 1.00f, 0.75f, 0.30f },
	{ 1.000f, 1.00f, 0.45f, 0.00f },
};

//Hard thirds, doubled stops. The seams are the look: terminal-yellow, scanner
//cyan and warning magenta, uninterpolated, like a badly calibrated CRT.
const Stop kCyberdeck[] = {
	{ 0.000f, 1.00f, 0.90f, 0.05f },
	{ 0.323f, 1.00f, 0.90f, 0.05f },
	{ 0.343f, 0.05f, 0.90f, 0.95f },
	{ 0.657f, 0.05f, 0.90f, 0.95f },
	{ 0.677f, 1.00f, 0.10f, 0.70f },
	{ 1.000f, 1.00f, 0.10f, 0.70f },
};

const Stop kHologram[] = {
	{ 0.000f, 0.15f, 0.85f, 0.95f },
	{ 0.500f, 0.90f, 1.00f, 1.00f },
	{ 1.000f, 0.15f, 0.85f, 0.95f },
};

const Stop kBloodNeon[] = {
	{ 0.000f, 0.65f, 0.00f, 0.15f },
	{ 0.500f, 1.00f, 0.15f, 0.20f },
	{ 0.800f, 1.00f, 0.45f, 0.35f },
	{ 1.000f, 0.65f, 0.00f, 0.15f },
};

//Deliberately flat. A tube's worth of one colour, for when the breakaway or
//the path should carry the whole look and the palette should get out of the
//way.
const Stop kMono[] = {
	{ 0.000f, 1.00f, 1.00f, 1.00f },
	{ 1.000f, 1.00f, 1.00f, 1.00f },
};

struct Definition
{
	const Stop* stops;
	int count;
	const char* name;
};

template< int N >
constexpr Definition define( const Stop ( &stops )[ N ], const char* name )
{
	return Definition { stops, N, name };
}

const Definition kDefinitions[] = {
	Definition { nullptr, 0, "Colour 1" },
	Definition { nullptr, 0, "Colour 1 > 2" },
	define( kMiami, "Miami" ),
	define( kOutrun, "Outrun" ),
	define( kSunsetDrive, "Sunset Drive" ),
	define( kLaser, "Laser" ),
	define( kVice, "Vice" ),
	define( kChrome, "Chrome" ),
	define( kUltraviolet, "Ultraviolet" ),
	define( kNeonNoir, "Neon Noir" ),
	define( kAcid, "Acid" ),
	define( kSodium, "Sodium" ),
	define( kCyberdeck, "Cyberdeck" ),
	define( kHologram, "Hologram" ),
	define( kBloodNeon, "Blood Neon" ),
	define( kMono, "Mono" ),
};

static_assert( sizeof( kDefinitions ) / sizeof( kDefinitions[ 0 ] ) == static_cast< size_t >( Palette::Count ),
               "every Palette enumerator needs a definition, and in the same order" );

/// Interpolate a stop list. The list is sorted and its first stop sits at 0,
/// so a position below the first stop cannot happen and is not handled.
Rgb sample( const Definition& definition, float position )
{
	position = std::clamp( position, 0.0f, 1.0f );

	for( int i = 1; i < definition.count; ++i )
	{
		const Stop& previous = definition.stops[ i - 1 ];
		const Stop& current  = definition.stops[ i ];
		if( position > current.position )
			continue;

		const float span = current.position - previous.position;
		//Two stops close together are how a hard edge is written -- Cyberdeck
		//depends on it. Guard the divide rather than forbidding it.
		const float t = span > 0.0f ? ( position - previous.position ) / span : 0.0f;

		return Rgb {
			previous.r + ( current.r - previous.r ) * t,
			previous.g + ( current.g - previous.g ) * t,
			previous.b + ( current.b - previous.b ) * t,
		};
	}

	const Stop& last = definition.stops[ definition.count - 1 ];
	return Rgb { last.r, last.g, last.b };
}
} // namespace

const char* PaletteName( Palette palette )
{
	const int index = static_cast< int >( palette );
	if( index < 0 || index >= static_cast< int >( Palette::Count ) )
		return "?";
	return kDefinitions[ index ].name;
}

Rgb PaletteLookup( Palette palette, float position, const Rgb& colour1, const Rgb& colour2 )
{
	//Wrap rather than clamp. The stroke coordinate walks the palette round,
	//and clamping would park every long tube on its palette's end colour.
	position = position - std::floor( position );

	switch( palette )
	{
	case Palette::Primary:
		return colour1;

	case Palette::PrimarySecondary:
		return Rgb {
			colour1.r + ( colour2.r - colour1.r ) * position,
			colour1.g + ( colour2.g - colour1.g ) * position,
			colour1.b + ( colour2.b - colour1.b ) * position,
		};

	default:
		break;
	}

	const int index = static_cast< int >( palette );
	if( index < kFirstBakedPalette || index >= static_cast< int >( Palette::Count ) )
		return colour1;

	return sample( kDefinitions[ index ], position );
}

std::vector< float > BakePaletteTable()
{
	const int rows = static_cast< int >( Palette::Count );
	std::vector< float > table( static_cast< size_t >( rows ) * kPaletteSize * 4, 0.0f );

	for( int row = kFirstBakedPalette; row < rows; ++row )
	{
		for( int x = 0; x < kPaletteSize; ++x )
		{
			//Texel centres, so entry 0 is position 0 and entry 255 is position
			//1 -- not 255/256, which would leave a seam where a wrapping
			//palette meets itself.
			const float position = static_cast< float >( x ) / static_cast< float >( kPaletteSize - 1 );
			const Rgb colour     = sample( kDefinitions[ row ], position );

			const size_t offset  = ( static_cast< size_t >( row ) * kPaletteSize + x ) * 4;
			table[ offset + 0 ]  = colour.r;
			table[ offset + 1 ]  = colour.g;
			table[ offset + 2 ]  = colour.b;
			table[ offset + 3 ]  = 1.0f;
		}
	}

	return table;
}

} // namespace outrun
