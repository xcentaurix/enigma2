#pragma once

// Shared-memory transport for e3ipc::WireOpcode (see gwireopcode.h), the
// cross-process replacement for gRC's in-process opcode queue
// (lib/gdi/grc.h's `gOpcode queue[MAXSIZE]`, guarded by a plain
// non-process-shared pthread mutex/cond) once the compositor moves out of
// the UI process. Deliberately shaped as closely as possible after gRC's
// existing ring - same single-producer/single-consumer fixed-slot circular
// buffer, same "lock, check, retry with a short sleep if full" backpressure
// gRC::submit() already uses (grc.cpp) - rather than a from-scratch design,
// to keep the amount of genuinely new synchronization logic as small as
// possible.
//
// One gShmOpcodeRing instance wraps one shared-memory segment containing:
//   - a fixed-size Header (the process-shared mutex/cond plus both ring
//     bookkeeping - see below)
//   - `slotCount` WireOpcode slots (the opcode ring proper)
//   - an `arenaSize`-byte arena (a second, byte-granular ring living right
//     behind the opcode slots) for whatever a WireOpcode's `bytesA` field
//     points into - text, region rect lists, gradient colors, small inline
//     blit pixel data.
//
// Lifetime contract for arena payloads: the bytes a popped WireOpcode's
// bytesA refers to remain valid only until the *next* pop() call - pop()
// reclaims the previous slot's arena bytes for reuse by the producer right
// before it returns the next one. A consumer that needs a payload past
// that point must copy it out before calling pop() again. This mirrors how
// a socket recv buffer behaves and keeps the arena's own bookkeeping to a
// single read cursor instead of a second, independent free-list.

#include <cstdint>
#include <cstddef>
#include <pthread.h>
#include "gwireopcode.h"

namespace e3ipc
{

class gShmOpcodeRing
{
public:
	gShmOpcodeRing() = default;
	~gShmOpcodeRing();

	gShmOpcodeRing(const gShmOpcodeRing &) = delete;
	gShmOpcodeRing &operator=(const gShmOpcodeRing &) = delete;

	// Compositor (e3-compd) side: creates and owns the shared-memory
	// segment under POSIX shm name `name` (e.g. "/e3-compd-opcodes";
	// must start with '/' and contain no further '/', per shm_open(3)).
	// Fails (returns false) if a segment of that name already exists -
	// stale segments from a previous crashed instance must be
	// shm_unlink()'d by the caller (e.g. the process supervisor) before
	// respawning, not silently reused, since their bookkeeping could be
	// left mid-update by a producer/consumer that died holding the mutex.
	bool create(const char *name, uint32_t slotCount, uint32_t arenaBytes);

	// UI-process side: attaches to a segment created by create(), sized
	// and configured from the segment's own header - slotCount/arenaBytes
	// need not be known by the attaching side.
	bool attach(const char *name);

	// Releases this process's mapping of the segment. The creating side
	// (create()) additionally shm_unlink()s the name so a clean shutdown
	// doesn't leave a stale segment behind for the next run to trip over.
	void close();

	bool isOpen() const { return m_header != nullptr; }

	// Producer side (UI process). Copies `op` into the next slot and, if
	// `payload`/`payloadLen` is non-null/non-zero, first copies that many
	// bytes into the arena and fills op.bytesA with the resulting
	// reference (the caller builds `op` with bytesA left default and
	// gets it back via `outOp` - `op` itself is taken by value so the
	// caller's own copy is left untouched). Blocks (bounded by a fixed
	// number of short sleeps, matching gRC::submit()'s existing full-queue
	// backpressure pattern) if either the slot ring or the arena doesn't
	// currently have room; returns false if it gives up or if the ring has
	// been shut down (see requestShutdown()).
	bool push(WireOpcode op, const void *payload, uint32_t payloadLen);

	// Consumer side (e3-compd). Blocks up to `timeoutMs` (0 = forever)
	// for the next opcode; returns false on timeout or shutdown. See the
	// class comment above for the arena payload lifetime this call
	// implies for whatever the *previous* popped opcode's bytesA
	// referenced.
	bool pop(WireOpcode &outOp, int timeoutMs);

	// Resolves a WireBytes reference into a pointer valid in *this*
	// process's own mapping of the arena. Only meaningful between a
	// pop() call and the next one (see the lifetime contract above).
	const uint8_t *arenaPtr(const WireBytes &ref) const;

	// Wakes any blocked push()/pop() and makes both start returning
	// false, so a process that's exiting (or has detected its peer die)
	// can unwind cleanly instead of a thread sitting in pop()/push()
	// forever. Safe to call from either side; idempotent.
	void requestShutdown();

private:
	// Everything here lives IN the shared segment and is therefore
	// touched by both processes - no pointers, no anything that isn't
	// meaningful identically in both address spaces.
	struct Header
	{
		pthread_mutex_t mutex;
		pthread_cond_t notEmpty;
		uint32_t slotCount;
		uint32_t arenaSize;
		uint32_t rp, wp;           // opcode slot indices, mod slotCount
		uint32_t arenaRp, arenaWp; // arena byte offsets, mod arenaSize
		bool shuttingDown;
	};

	Header *m_header = nullptr;
	WireOpcode *m_slots = nullptr;
	uint8_t *m_arena = nullptr;
	void *m_mapping = nullptr;
	size_t m_mappingSize = 0;
	bool m_owner = false; // true for the side that called create()
	char m_name[256] = {};

	// This process's own record of the most recently popped opcode's
	// arena payload, used by pop() to defer reclaiming it until the
	// following call - see the class comment's lifetime contract.
	// Consumer-side-only state (never touched by the producer), so it
	// lives here rather than in the shared Header.
	WireBytes m_pendingRelease;

	static size_t segmentSize(uint32_t slotCount, uint32_t arenaBytes);
	bool mapExisting(const char *name);
};

} // namespace e3ipc
