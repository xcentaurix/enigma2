#pragma once

// Wire-format counterpart of gOpcode (lib/gdi/grc.h), for the shared-memory
// opcode ring between the UI process and the compositor process (e3-compd)
// introduced by the Enigma3 compositor-split. gOpcode itself cannot cross a
// process boundary as-is: most of its payload is either a raw pointer
// (char *, gPixmap *, eTextPara *) into the submitting process's own heap,
// or an STL container (gRegion, std::vector<gRGB>) with the same problem.
// WireOpcode is the POD, safe-to-memcpy-into-shared-memory equivalent that
// gRC's producer side builds from a gOpcode before handing it to
// gShmOpcodeRing, and that the compositor's consumer side turns back into
// backend calls.
//
// Deliberately flat rather than a union of the per-opcode payload structs
// gOpcode itself uses: a C++ union of members with non-trivial constructors
// (eRect, gRGB, WireBytes all have user-provided constructors) requires
// manual placement-new/destroy discipline to use safely, which is exactly
// the kind of landmine to avoid in a struct two different processes read
// and write via a raw shared mapping. The few extra bytes per slot from
// carrying every field unconditionally is a non-issue at this opcode rate.
//
// Deliberately NOT carried by this transport at all:
//  - setPalette/mergePalette: gEGLDC is truecolor-only and already ignores
//    palette opcodes; no reason to carry them across the wire either.
//  - setCompositing: gCompositingData/m_code (lib/gdi/compositing.h) is
//    dead, never-implemented scaffolding - gCompositingData::execute() has
//    no body anywhere in the tree and nothing ever calls it. Dropped
//    rather than migrated.
//  - renderPara: needs eTextPara's shaped glyph run flattened into
//    (glyph_key, x, y, color) tuples in the UI process before it can cross
//    the wire, and glyph_key itself needs rekeying off stable identity
//    (font path/size/glyph index) instead of today's in-process FreeType
//    face_id pointer. Both are separate, scoped follow-up work - see the
//    Enigma3 compositor-split plan. serializeOpcode() below returns false
//    for gOpcode::renderPara so callers can see the gap explicitly instead
//    of silently dropping text.
//  - gOpcode::renderText with a non-null `offset` out-parameter (grc.h's
//    prenderText::offset - used by marquee/measuring callers to get a
//    result back): an async, fire-and-forget wire opcode has nowhere to
//    write a synchronous return value back to. serializeOpcode() returns
//    false for these too rather than silently dropping the reported
//    offset; supporting them needs a reply channel, out of scope here.

#include <cstdint>
#include <lib/gdi/erect.h>
#include <lib/gdi/epoint.h>
#include <lib/gdi/esize.h>
#include <lib/gdi/gpixmap.h> // gRGB, gColor

namespace e3ipc
{

// Which real display device on the compositor side an opcode targets -
// replaces gOpcode::dc (a gDC*, meaningless across the process boundary).
// The compositor process owns both the main EGL display and the LCD panel:
// today they already share one gRC dispatch loop and one render thread
// (gLCDDC is just another gDC subclass fed through the same queue), so
// splitting them onto separate transports isn't worth a second ring.
enum class WireDcId : uint8_t
{
	MainDisplay = 0,
	Lcd = 1,
};

// Mirrors gOpcode::Opcode (grc.h) for exactly the subset this transport
// carries - see the file-level comment above for what's excluded and why.
enum class WireOp : uint8_t
{
	Fill,
	FillRegion,
	Clear,
	Blit,
	Rectangle,
	Line,
	RenderText,
	SetBackgroundColor,
	SetForegroundColor,
	SetBackgroundColorRGB,
	SetForegroundColorRGB,
	SetGradient,
	SetRadius,
	SetBorder,
	SetOffset,
	SetClip,
	AddClip,
	PopClip,
	Flush,
	WaitVSync,
	Flip,
	Notify,
	EnableSpinner,
	DisableSpinner,
	IncrementSpinner,
	SendShow,
	SendHide,
	Shutdown,
};

// A reference into the opcode ring's companion byte arena (see
// gShmOpcodeRing) for whatever doesn't fit in WireOpcode's own fixed
// fields - strings, gRegion's rect list, gradient color lists, and (for
// non-accelerated blits only, see WireOpcode's blit fields below) inline
// pixel bytes. `offset` is relative to the arena's own base, never an
// absolute pointer: the two processes map the same shared-memory segment
// at different virtual addresses. Valid only until the ring's next pop()
// call on the consumer side - see gShmOpcodeRing's own comment for the
// lifetime contract.
struct WireBytes
{
	uint32_t offset = 0;
	uint32_t length = 0;
};

struct WireOpcode
{
	WireOp op = WireOp::Flush;
	WireDcId dc = WireDcId::MainDisplay;

	// --- geometry, reused by field name across opcode types (see the
	// per-field comments) rather than given one name per opcode, to keep
	// this struct's size down without resorting to a union. ---
	eRect rectA;   // fill.area / rectangle.area / renderText.area / blit.position
	eRect rectB;   // blit.clip / fillRegion|setClip|addClip .extends (gRegion::extends)
	ePoint pointA; // line.start / setOffset.value / sendShow|sendHide .point
	ePoint pointB; // line.end
	eSize sizeA;   // sendShow|sendHide .size

	// --- color/appearance ---
	gRGB colorRGB;    // setForegroundColorRGB|setBackgroundColorRGB .color / setBorder.color / renderText.bordercolor
	gColor colorIdx;  // setForegroundColor|setBackgroundColor .color

	// --- small scalars, reused by field name (see per-opcode comments in
	// gwireopcode.cpp's serializeOpcode()) ---
	int flags = 0;         // blit.flags / renderText.flags
	int intA = 0;           // renderText.border / setBorder.width / setOffset.rel / rectangle.useNew (0/1) / setRadius.radius
	int intB = 0;           // renderText.markedpos
	int intC = 0;           // renderText.scrollpos
	uint8_t edges = 0;       // setRadius.edges
	uint8_t orientation = 0; // setGradient.orientation
	bool alphablend = false; // setGradient.alphablend
	int fullSize = 0;        // setGradient.fullSize

	// --- blit-only geometry (position/clip are rectA/rectB above) ---
	int width = 0;
	int height = 0;
	int stride = 0;
	int bpp = 0;

	// --- variable-length payload in the arena, reused by field name:
	//   RenderText   -> UTF-8 text (NOT necessarily NUL-terminated - use .length)
	//   FillRegion/SetClip/AddClip -> gRegion::rects, as a packed eRect[]
	//   SetGradient  -> gRGB[] gradient stop colors
	//   Blit         -> raw pixel bytes, ONLY for pixmaps below gAccel's
	//                    accel-eligibility threshold (see gpixmap.cpp's
	//                    is_a_candidate_for_accel). An accel/ION-backed
	//                    pixmap needs its dma-buf fd passed over the
	//                    control socket and imported via the existing
	//                    createTextureFromDmabuf() path instead - not yet
	//                    implemented, see serializeOpcode()'s TODO.
	WireBytes bytesA;
};

} // namespace e3ipc
