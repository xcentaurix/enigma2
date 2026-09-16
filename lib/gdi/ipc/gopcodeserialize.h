#pragma once

// Producer-side (UI process) glue: turns an in-process gOpcode (lib/gdi/grc.h)
// into an e3ipc::WireOpcode and pushes it through a gShmOpcodeRing. Kept
// separate from gwireopcode.h (the wire format itself, meaningful to both
// processes) and gshmopcodering.h (the transport, likewise needed by both)
// since this is the one piece that depends on gOpcode/gRegion/gPixmap and
// is therefore only relevant to whichever side still has real gOpcodes to
// serialize - the UI process, not the compositor.

#include "gshmopcodering.h"
#include "gwireopcode.h"

struct gOpcode;

namespace e3ipc
{

// Converts `in` to wire form and pushes it onto `ring`, targeting `dc`.
// Returns false, and pushes nothing, for any gOpcode this transport
// doesn't support yet - see gwireopcode.h's file comment for the current
// list (renderPara, a renderText with a non-null out-parameter,
// setCompositing, setPalette/mergePalette, and a blit of an accel/ION-
// backed gPixmap) - and equally for a push() that failed for one of
// gShmOpcodeRing's own reasons (ring or arena full past its retry budget,
// or the ring shut down).
bool serializeOpcode(const gOpcode &in, WireDcId dc, gShmOpcodeRing &ring);

} // namespace e3ipc
