#pragma once

// Producer-side (UI process) connection to a running compositor process
// (e3-compd). Wraps an attached gShmOpcodeRing plus serializeOpcode()
// (gopcodeserialize.h) behind a single submit() entry point, shaped after
// gRC::submit() (lib/gdi/grc.h) so it can eventually stand in for it.
//
// NOT wired into gRC or main/enigma.cpp's startup yet, and nothing in the
// normal enigma2 build path constructs one of these - see the Enigma3
// compositor-split plan. Actually replacing gRC's internals needs the
// AutoInit-ordering and gMainDC-proxy work the plan describes, which
// deserves its own reviewable slice on top of a working transport rather
// than being folded into it sight-unseen.
//
// No reconnect/retry logic here either: connect() either finds a segment
// e3-compd already created, or it doesn't. A supervisor that respawns
// e3-compd and reconnects this client on the next draw is later, scoped
// work (see the plan's fault-isolation section), not part of this class.

#include "gopcodeserialize.h"

struct gOpcode;

namespace e3ipc
{

class gShmOpcodeClient
{
public:
	// Attaches to the shared-memory ring e3-compd created under `shmName`
	// (e.g. "/e3-compd-opcodes"). `dc` is which real display this client's
	// opcodes are destined for (see WireDcId) - a single gShmOpcodeClient
	// only ever targets one.
	bool connect(const char *shmName, WireDcId dc);

	void disconnect() { m_ring.close(); }
	bool isConnected() const { return m_ring.isOpen(); }

	// Serializes and forwards `op` to e3-compd. Returns false for
	// whatever serializeOpcode() itself declines (see gwireopcode.h's
	// file comment for the current list) or if the underlying ring push
	// failed (full past its retry budget, or the ring was shut down -
	// e.g. e3-compd crashed).
	bool submit(const gOpcode &op);

private:
	gShmOpcodeRing m_ring;
	WireDcId m_dc = WireDcId::MainDisplay;
};

} // namespace e3ipc
