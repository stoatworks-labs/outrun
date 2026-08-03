#include "Paths.h"

namespace outrun
{

const char* PathName( Path path )
{
	switch( path )
	{
	case Path::Grid:      return "Grid";
	case Path::Lissajous: return "Lissajous";
	case Path::Hex:       return "Hex";
	case Path::Circuit:   return "Circuit";
	case Path::Skyline:   return "Skyline";
	case Path::Rings:     return "Rings";
	case Path::Star:      return "Star";
	case Path::Waveform:  return "Waveform";
	default:              return "?";
	}
}

} // namespace outrun
