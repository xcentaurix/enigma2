// Compositor-process consumer for the Enigma3 compositor-split (see the
// plan's "Milestone 1" design). Creates the shared-memory opcode ring
// (lib/gdi/ipc/gshmopcodering.h), and - when built with HAVE_EGL and
// HAVE_DREAMBOX_EGL (the same flags gEGLDC/DreamboxWindowProvider are
// already gated behind elsewhere in this tree, see lib/gdi/Makefile.inc) -
// actually drives a real gEGLDC with them, reconstructing each WireOpcode
// into a heap-allocated gOpcode exactly the way gPainter's queuing side
// (lib/gdi/grc.cpp) already does, so gEGLDC::exec()/gDC::exec() consume
// and free it identically to how they always have. Falls back to logging
// opcodes when built without those flags (e.g. for a host/simulator build
// with no real EGL target), so this file compiles and runs everywhere
// e3-compd's link target is enabled at all.
//
// Deliberately single-threaded: unlike gRC's in-process design (a
// dedicated render thread separate from whatever submits opcodes), there
// is only one thread here, and it plays both roles gRC::thread() would -
// popping opcodes AND calling exec() - which is exactly why initEGL() can
// simply be called once, up front, on this same thread (EGL contexts are
// per-thread; see egl_init.cpp's and grc.cpp's own comments on this).
//
// Usage: e3-compd [shm-name]  (default "/e3-compd-opcodes")

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <sys/mman.h> // shm_unlink

#include <lib/gdi/ipc/gshmopcodering.h>
#include <lib/gdi/ipc/gwireopcode.h>

#if defined(HAVE_EGL) && defined(HAVE_DREAMBOX_EGL)
#define E3_COMPD_REAL_BACKEND 1
#include <lib/gdi/egl/gegldc.h>
#include <lib/gdi/egl/platform/dreambox/dreambox_window_provider.h>
#include <lib/gdi/fb.h>
#include <lib/gdi/gpixmap.h>
#include <lib/gdi/grc.h>
#include <lib/gdi/region.h>
#endif

using namespace e3ipc;

namespace
{

volatile sig_atomic_t g_stop = 0;

void handleSignal(int)
{
	g_stop = 1;
}

const char *opName(WireOp op)
{
	switch (op)
	{
	case WireOp::Fill: return "Fill";
	case WireOp::FillRegion: return "FillRegion";
	case WireOp::Clear: return "Clear";
	case WireOp::Blit: return "Blit";
	case WireOp::Rectangle: return "Rectangle";
	case WireOp::Line: return "Line";
	case WireOp::RenderText: return "RenderText";
	case WireOp::SetBackgroundColor: return "SetBackgroundColor";
	case WireOp::SetForegroundColor: return "SetForegroundColor";
	case WireOp::SetBackgroundColorRGB: return "SetBackgroundColorRGB";
	case WireOp::SetForegroundColorRGB: return "SetForegroundColorRGB";
	case WireOp::SetGradient: return "SetGradient";
	case WireOp::SetRadius: return "SetRadius";
	case WireOp::SetBorder: return "SetBorder";
	case WireOp::SetOffset: return "SetOffset";
	case WireOp::SetClip: return "SetClip";
	case WireOp::AddClip: return "AddClip";
	case WireOp::PopClip: return "PopClip";
	case WireOp::Flush: return "Flush";
	case WireOp::WaitVSync: return "WaitVSync";
	case WireOp::Flip: return "Flip";
	case WireOp::Notify: return "Notify";
	case WireOp::EnableSpinner: return "EnableSpinner";
	case WireOp::DisableSpinner: return "DisableSpinner";
	case WireOp::IncrementSpinner: return "IncrementSpinner";
	case WireOp::SendShow: return "SendShow";
	case WireOp::SendHide: return "SendHide";
	case WireOp::Shutdown: return "Shutdown";
	}
	return "?";
}

// Fallback consumer for a build without a real backend (see the file
// comment) - just proves the transport works end to end.
void logOpcode(const gShmOpcodeRing &ring, const WireOpcode &op)
{
	std::fprintf(stderr, "[e3-compd] dc=%d op=%s bytes=%u\n",
		static_cast<int>(op.dc), opName(op.op), op.bytesA.length);
	(void)ring;
}

#ifdef E3_COMPD_REAL_BACKEND

// Turns a WireOpcode back into a heap-allocated gOpcode matching exactly
// what gPainter's queuing methods (lib/gdi/grc.cpp) would have built, so
// gEGLDC::exec()/gDC::exec() - which own and free this payload themselves,
// see their own delete/Release() calls, cross-checked case by case against
// grc.cpp and gegldc.cpp while writing this - accept and dispose of it
// identically to any opcode that originated locally. opcode.dc is left
// null: gDC::exec() is invoked here as a plain member call (dc.exec(&o)),
// never as "o.dc->exec(&o)" the way gRC::thread() does it, and neither
// gDC::exec() nor gEGLDC::exec() ever reads o->dc themselves - it only
// mattered to gRC::thread()'s own dispatch/Release() bookkeeping, which
// doesn't exist in this single-consumer design at all.
//
// Returns false (and does nothing else) for whatever this function
// doesn't reconstruct - kept in exact lockstep with what
// lib/gdi/ipc/gopcodeserialize.cpp's serializeOpcode() declines to send in
// the first place, so this should never actually be reached for those;
// it's here so an unrecognized/future WireOp fails loudly (see the
// caller) instead of silently no-op'ing.
bool deserializeAndExec(const gShmOpcodeRing &ring, gEGLDC &dc, const WireOpcode &w)
{
	gOpcode o;
	o.opcode = gOpcode::flush; // harmless placeholder; every case below that reaches dc.exec(&o) overwrites it first
	o.dc = nullptr;

	switch (w.op)
	{
	case WireOp::Fill:
		o.opcode = gOpcode::fill;
		o.parm.fill = new gOpcode::para::pfillRect;
		o.parm.fill->area = w.rectA;
		break;

	case WireOp::FillRegion:
	{
		o.opcode = gOpcode::fillRegion;
		o.parm.fillRegion = new gOpcode::para::pfillRegion;
		gRegion region;
		region.extends = w.rectB;
		const uint8_t *bytes = ring.arenaPtr(w.bytesA);
		if (bytes)
		{
			size_t count = w.bytesA.length / sizeof(eRect);
			const eRect *rects = reinterpret_cast<const eRect *>(bytes);
			region.rects.assign(rects, rects + count);
		}
		o.parm.fillRegion->region = region;
		break;
	}

	case WireOp::Clear:
		// gDC::exec()'s clear case (grc.cpp) reuses parm.fill and never
		// reads ->area for it (see gPainter::clear() setting it to an
		// empty eRect() too) - only needs to be a valid pointer to delete.
		o.opcode = gOpcode::clear;
		o.parm.fill = new gOpcode::para::pfillRect;
		o.parm.fill->area = eRect();
		break;

	case WireOp::Blit:
	{
		const uint8_t *bytes = ring.arenaPtr(w.bytesA);
		if (!bytes || w.width <= 0 || w.height <= 0)
			return false;
		gPixmap *pixmap = new gPixmap(eSize(w.width, w.height), w.bpp, gPixmap::accelNever);
		// Matches gPainter::blit()'s pixmap->AddRef() (grc.cpp) - balanced
		// by executeBlit()'s BlitOpcodeGuard (gegldc.cpp) once this opcode
		// is processed, exactly as if it had come from gPainter directly.
		pixmap->AddRef();
		int rowBytes = std::min(w.stride, pixmap->surface->stride);
		for (int y = 0; y < w.height; ++y)
			std::memcpy(
				static_cast<uint8_t *>(pixmap->surface->data) + (size_t)y * pixmap->surface->stride,
				bytes + (size_t)y * w.stride,
				(size_t)rowBytes);

		o.opcode = gOpcode::blit;
		o.parm.blit = new gOpcode::para::pblit;
		o.parm.blit->pixmap = pixmap;
		o.parm.blit->flags = w.flags;
		o.parm.blit->position = w.rectA;
		o.parm.blit->clip = w.rectB;
		break;
	}

	case WireOp::Rectangle:
		o.opcode = gOpcode::rectangle;
		o.parm.rectangle = new gOpcode::para::prectangle;
		o.parm.rectangle->area = w.rectA;
		o.parm.rectangle->useNew = w.intA != 0;
		break;

	case WireOp::Line:
		o.opcode = gOpcode::line;
		o.parm.line = new gOpcode::para::pline;
		o.parm.line->start = w.pointA;
		o.parm.line->end = w.pointB;
		break;

	case WireOp::SetBackgroundColor:
		o.opcode = gOpcode::setBackgroundColor;
		o.parm.setColor = new gOpcode::para::psetColor;
		o.parm.setColor->color = w.colorIdx;
		break;

	case WireOp::SetForegroundColor:
		o.opcode = gOpcode::setForegroundColor;
		o.parm.setColor = new gOpcode::para::psetColor;
		o.parm.setColor->color = w.colorIdx;
		break;

	case WireOp::SetBackgroundColorRGB:
		o.opcode = gOpcode::setBackgroundColorRGB;
		o.parm.setColorRGB = new gOpcode::para::psetColorRGB;
		o.parm.setColorRGB->color = w.colorRGB;
		break;

	case WireOp::SetForegroundColorRGB:
		o.opcode = gOpcode::setForegroundColorRGB;
		o.parm.setColorRGB = new gOpcode::para::psetColorRGB;
		o.parm.setColorRGB->color = w.colorRGB;
		break;

	case WireOp::SetGradient:
	{
		o.opcode = gOpcode::setGradient;
		o.parm.gradient = new gOpcode::para::pgradient;
		const uint8_t *bytes = ring.arenaPtr(w.bytesA);
		if (bytes)
		{
			size_t count = w.bytesA.length / sizeof(gRGB);
			const gRGB *colors = reinterpret_cast<const gRGB *>(bytes);
			o.parm.gradient->colors.assign(colors, colors + count);
		}
		o.parm.gradient->orientation = w.orientation;
		o.parm.gradient->alphablend = w.alphablend;
		o.parm.gradient->fullSize = w.fullSize;
		break;
	}

	case WireOp::SetRadius:
		o.opcode = gOpcode::setRadius;
		o.parm.radius = new gOpcode::para::pradius;
		o.parm.radius->radius = w.intA;
		o.parm.radius->edges = w.edges;
		break;

	case WireOp::SetBorder:
		o.opcode = gOpcode::setBorder;
		o.parm.border = new gOpcode::para::pborder;
		o.parm.border->color = w.colorRGB;
		o.parm.border->width = w.intA;
		break;

	case WireOp::SetOffset:
		o.opcode = gOpcode::setOffset;
		o.parm.setOffset = new gOpcode::para::psetOffset;
		o.parm.setOffset->value = w.pointA;
		o.parm.setOffset->rel = w.intA;
		break;

	case WireOp::SetClip:
	case WireOp::AddClip:
	{
		o.opcode = (w.op == WireOp::SetClip) ? gOpcode::setClip : gOpcode::addClip;
		o.parm.clip = new gOpcode::para::psetClip;
		gRegion region;
		region.extends = w.rectB;
		const uint8_t *bytes = ring.arenaPtr(w.bytesA);
		if (bytes)
		{
			size_t count = w.bytesA.length / sizeof(eRect);
			const eRect *rects = reinterpret_cast<const eRect *>(bytes);
			region.rects.assign(rects, rects + count);
		}
		o.parm.clip->region = region;
		break;
	}

	case WireOp::PopClip:
		o.opcode = gOpcode::popClip;
		break;

	case WireOp::Flush:
		o.opcode = gOpcode::flush;
		break;

	case WireOp::WaitVSync:
		o.opcode = gOpcode::waitVSync;
		break;

	case WireOp::Flip:
		o.opcode = gOpcode::flip;
		break;

	case WireOp::Notify:
		// gRC::submit()'s forwarding path (grc.cpp) already fires the
		// UI process's own local gRC::notify signal synchronously for
		// this opcode (see its own comment for why) - it isn't forwarded
		// as anything meaningful here, so this backend just no-ops it
		// rather than fabricating a gOpcode::notify with no gDC-side
		// effect anyway (gDC::exec()'s notify case is a bare no-op too).
		return true;

	case WireOp::EnableSpinner:
		o.opcode = gOpcode::enableSpinner;
		break;

	case WireOp::DisableSpinner:
		o.opcode = gOpcode::disableSpinner;
		break;

	case WireOp::IncrementSpinner:
		o.opcode = gOpcode::incrementSpinner;
		break;

	case WireOp::SendShow:
		o.opcode = gOpcode::sendShow;
		o.parm.setShowHideInfo = new gOpcode::para::psetShowHideInfo;
		o.parm.setShowHideInfo->point = w.pointA;
		o.parm.setShowHideInfo->size = w.sizeA;
		break;

	case WireOp::SendHide:
		o.opcode = gOpcode::sendHide;
		o.parm.setShowHideInfo = new gOpcode::para::psetShowHideInfo;
		o.parm.setShowHideInfo->point = w.pointA;
		o.parm.setShowHideInfo->size = w.sizeA;
		break;

	case WireOp::Shutdown:
		return true; // handled by the caller's loop, nothing to exec

	default:
		return false;
	}

	dc.exec(&o);
	return true;
}

#endif // E3_COMPD_REAL_BACKEND

} // namespace

int main(int argc, char **argv)
{
	const char *shmName = argc > 1 ? argv[1] : "/e3-compd-opcodes";
	const uint32_t kSlotCount = 4096;
	const uint32_t kArenaBytes = 4 * 1024 * 1024; // generous for now - real sizing needs measuring against actual opcode traffic (see the plan's verification step)

	// A segment left behind by a crashed previous instance would make
	// create() below fail (see gShmOpcodeRing::create()'s own comment).
	// This has no supervisor yet to distinguish "stale" from "another
	// instance is already running", so it just clears any existing
	// segment unconditionally on startup; a real supervisor needs to make
	// that distinction properly before this ships.
	shm_unlink(shmName);

	gShmOpcodeRing ring;
	if (!ring.create(shmName, kSlotCount, kArenaBytes))
	{
		std::fprintf(stderr, "[e3-compd] failed to create shared opcode ring '%s'\n", shmName);
		return 1;
	}
	std::fprintf(stderr, "[e3-compd] listening on '%s' (%u slots, %u byte arena)\n",
		shmName, kSlotCount, kArenaBytes);

	signal(SIGINT, handleSignal);
	signal(SIGTERM, handleSignal);

#ifdef E3_COMPD_REAL_BACKEND
	// fbClass must exist (and be in the right video mode) before the
	// window provider's init() below - it describes the *existing*
	// framebuffer memory as EGL pixmaps (see DreamboxWindowProvider's own
	// class comment), it doesn't allocate its own. Constructing one
	// directly (rather than via this codebase's usual eAutoInitPtr
	// mechanism, which needs an eInit run loop this single-purpose binary
	// doesn't have) just opens /dev/fb0 and reads back whatever mode is
	// already active - if that's not the mode you want, call
	// fb.SetMode(xres, yres, 32) here explicitly before reading it back;
	// unverified whether that's needed on real hardware or whether the
	// mode already set by the bootloader/kernel is always sufficient (the
	// in-process AutoInit path - egl_init.cpp - never calls SetMode()
	// itself either, so this mirrors that, not a gap specific to this file).
	fbClass fb;
	int xres = 1920, yres = 1080, bpp = 32;
	fb.getMode(xres, yres, bpp);

	// gEGLDC's destructor unconditionally does "delete m_window_provider"
	// (gegldc.cpp) - this MUST be heap-allocated, not a stack object, or
	// that's undefined behavior.
	DreamboxWindowProvider *provider = new DreamboxWindowProvider();
	if (!provider->init(xres, yres))
	{
		std::fprintf(stderr, "[e3-compd] window provider init failed (%dx%d)\n", xres, yres);
		delete provider;
		return 1;
	}

	gEGLDC dc(provider, xres, yres);
	// Must run on THIS thread: EGL contexts are per-thread, and this is
	// the only thread there is here - it plays the role gRC::thread()
	// plays in the in-process design (see the file comment above).
	if (!dc.initEGL())
		std::fprintf(stderr, "[e3-compd] gEGLDC::initEGL() failed - opcodes will be accepted but nothing will render\n");
#endif

	WireOpcode op;
	while (!g_stop)
	{
		if (!ring.pop(op, 200)) // 200ms poll so SIGINT/SIGTERM get noticed promptly without a self-pipe
			continue;

#ifdef E3_COMPD_REAL_BACKEND
		if (!deserializeAndExec(ring, dc, op))
			std::fprintf(stderr, "[e3-compd] dropped unrecognized/unsupported op=%s\n", opName(op.op));
#else
		logOpcode(ring, op);
#endif
		if (op.op == WireOp::Shutdown)
			break;
	}

	std::fprintf(stderr, "[e3-compd] shutting down\n");
#ifdef E3_COMPD_REAL_BACKEND
	// dc's destructor (running at the end of this scope, on this same
	// thread) calls cleanupEGL() itself - see gEGLDC::~gEGLDC() - so no
	// explicit call is needed here; relying on that only works because
	// this thread is the one that called initEGL() above, which is the
	// whole point of keeping this binary single-threaded (see the file
	// comment).
#endif
	ring.requestShutdown();
	return 0;
}
