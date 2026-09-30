# Deferred interactions, context and entity cleanup

Build25492057; static evidence only. RVAs/offsets are hexadecimal; virtual slots
decimal. [Manifest](../artifacts/build-25492057/interaction-lifecycle-map.json).
This continues the [marker actions](marker-actions-map-25492057.md) and
[crew actions](crew-actions-map-25492057.md). No game execution or binary changes.

## Queue ordering

Handler Quant is `766FE0/5E4D90`. `766ED0` is dynamic receiver dispatch, not
Quant; the previous frontier's label is corrected. Entity Quant
`71D020/597D50` calls it when entity540 exists and handler48 is true, unless
the outer entity38 bit5 gate skips work. Actor Quant `7EFD10` reaches this base
work after weaponry update when actor38 mask220 is clear. This is local call
order, not a complete scene/thread scheduler model.

Quant processes these stages:

1. Swap live effects70/78/80 with global scratch1571BF0/BF8/C00. Walk detached
   rows in stored order, reappend zero/future deadlines, dispatch due effect names.
2. Destroy detached effect token values and clear the scratch end.
3. Swap live actions88/90/98 with scratch1571C10/C18/C20. Walk the detached
   rows in stored order, reappend zero/future deadlines and execute due actions.
4. Destroy detached action values, poll revolute-bone event bindings, recompute
   needs-Quant48 and reschedule the owner if it changes.

Each nonzero row reads the clock separately and compares unsigned values.
Deadline0 is always retained, even after the clock advances. Input delay0 still
queues an absolute deadline; it does not execute inline, and resulting deadline0
has the sentinel behavior above.

An effect callback can enqueue an action before the action swap, allowing it to
run in the same Quant if due. An action queued during the action stage waits for
a later Quant. Effects queued after their swap wait for a later effect stage.
Coalescing, `kill_delay` and resets search the **live** vectors. They cannot
remove an unprocessed detached row through these paths; earlier rows already
reappended are live again.

The scratch vectors are process-global. TLS accesses are static-initialization
guards, not per-thread storage. No recursive/concurrent Quant guard is visible
here. Scheduler serialization and recursive reachability remain leads, not
claims of a running-game race.

## Context copying does not retain entities

`76DCD0/5EC280` initializes a190-byte context and presence flags.
`748990/5C4710` copies raw/scalar fields plus owned token values18/88/128.
`76DD60/5EC320` destroys only those tokens. These operations contain no entity
reference increments/decrements or SoftLinkable registrations. The precise
object represented by copied field8 remains open.

GEM `5EC500` names bit16 `flags.frozen` and asserts that the requested state
differs. Game `76DEE0(ctx,1)` sets16 and clears24. It does not retain entities.
Thaw (`...,0`) performs the following operations, outlined in GEM `5EC5B0`:

- Rebind component80 by token88 when lengthD8 is nonzero and context entity0 is
  an actor, using the actor's B90 component collection; update presence bit27.
- Rebind volume10 by token18 when length68 is nonzero, using entity500's volume
  collection; update bit25. This is a volume, not a skeleton bone.
- Clear pointersE8/120, frozen16 and flags22/24/29.

Quant thaws **before** overwriting context entity0 with the handler owner and
updating bit23. It does not generically refresh every copied raw pointer.

A due action with nonnull handler owner selects `synced_global`/`entity_local`
RNG context from owner3A bit0. It calls action slot3 with a fresh owner context
or the thawed saved context. Saved storage is then destroyed/freed, and the
previous RNG context is restored when nonnull. Delay's slot3 executes children
through their slot2 wrappers, preserving their synchronization gates. The
null-owner branch skips execution **and** capture release; normal entity-owned
calling and teardown invariants therefore matter.

The marker note's earlier word “retaining” has been corrected: copying preserves
values, without establishing that referenced entities survive until the deadline.

## Explicit cleanup and cancellation differ

`7676B0/5E5460` stops every linked sound handle via4CDAD0, destroys aliases,
empties bone/effect rows, explicitly releases live action captures through
`769A70/5E7AD0`, clears dynamic receivers and clears inherited data storage.
It does not itself recompute bytes48/68.

Game-entity event3 (`71A7C0/595570`, `destroy:ge`) invokes that clear after
notifying extenders with their event6. These are different event-number domains.
This connects entity destruction to the previous map's sound-stop/repeat-removal
bridge for linked aliases. Other sound publishers remain separate.

Handler destructor `769910/5E7940` nulls owner40 and frees value vectors. GEM
asserts **`calls.empty()`** first. `7696A0/5E74C0` and row destructor
`768860/5E6590` destroy string/token values, not captured contexts. Explicit
capture cleanup is therefore distinct from ordinary vector destruction.

Named cancellation `766CB0/5E4A60` removes the first matching live row from each
queue. Action compaction copies raw capture pointers and calls only the value
destructor, never capture release. A capture in the removed row is consequently
not released by this local path. Actual captured named-cancellation usage and
possible outside aliases remain to investigate; no runtime leak is asserted.

Global reload `7629F0` destroys interned event descriptors, invokes registry
reload helpers, resets action IDs, rebinds definitions, then resets scene handlers
through `768280/5E6000`. That handler reset rebuilds bone bindings, clears live
action captures/effects and refreshes48. It does not perform full sound/receiver
clear. Registry helper internals and callbacks during reload remain partial.

## Bone motion produces paired events

`768670/5E63A0` emits stop events for active old bindings before replacing them.
Definition110..118 rows have stride68: bone token0, start ID60, stop ID62.
Names resolve against entityB0's model; only eRevoluteBone casts are retained.
Handler50 rows have stride10: raw bone0, start8, stopA, activeC.

Quant compares bone slot22's output integer to mutable147D69C (file initial1).
`5C6750/43D820` returns truncation of `abs(double228-double200)/float1F8` when
228 differs from200. Equality returns mutable1556DE8 (GEM13BE1A8), whose full
producer remains in the clock frontier. This is a remaining-step calculation,
not an enum inferred from RTTI.

False→true sets extra context token128 to `forward` when double200-double220>0,
otherwise `reverse`, and sets flag30. True→false uses a fresh context and the
stop ID. ActiveC changes before callback dispatch. FFFF IDs are ignored. These
extra tokens differ from the dynamic entity tags in human rowing conditions.

This closes a cannon producer link: `92B980/7B2E50` consumes the paired IDs,
updates active-motion countC50 and refreshes aim animation on zero/nonzero
transitions. Its gun/mgun IDs take the firing/crew-animation branch already
mapped in crew actions. Step/target writers and bone replacement remain open.

## Dynamic receivers enter extenders

`76ABE0` executes scripted layers before `766ED0`. Registration `766E70`
appends4-byte rows for ID!=FFFF without deduplication: event uint16 at0,
extender index byte2. `799440` finds the first extender name in entity578's
vector and returns its index byte, orFF.

Dispatch snapshots receiver begin/end, sign-extends each matching index and
bounds-checks it against the current extender vector using unsigned comparison.
It then invokes **extender slot8(eventID,context)**. Invalid indices diagnose
and INT3. All matching rows run. Rows are not copied and extenders are not
retained; replacement and reentrant mutation remain lifetime questions.

All direct registration xrefs are captured, with four callers:

| Caller | Bounded registration/consumer contract |
|---|---|
| `79E120/61E780`, eLightExtender | Event0 binds definitionD2, `light_break/on/off`, definitionC8/CA. `79E510/61EB60` matches context volume's first tag in one branch, visits all lights for break, delegates on/off and conditionally refreshes for C8/CA. |
| `91A820/7A1B10`, eShieldExtender | Event0 binds `cut_parts`. `91A930/7A1C20` selects first tags with prefix `shield`, marks volume28 bit4, calls982A10 and modifies volumeC masks using extender8A/8B. |
| `92B1B0/7B26C0`, eCannonExtender | Event0 binds gun/mgun and both IDs of every definition bone-motion pair; slot8 consumes them as above. |
| `7C0A60`, structure helper | Resolves `check_structure`, stores ID C2 and registers its owning extender's index; called by structure-root/bone maintenance paths. |

This does not rule out indirect/inlined registration. The broad context-destructor
xref anchor is truncated at128, so it does not establish whole-program lifetime.

## Checks and continuation

35 contracts/slices,26 game/editor pairs,38 fields and18 RTTI tables. LLVM checks
61 function bodies and6726 instruction starts against Ghidra, allowing its
separate LOCK-prefix formatting. `verify_fine_map.py` checks fingerprints,
tables and the writable threshold's file-initial bytes. These checks establish
evidence consistency, not semantic completeness or a callable ABI.

Continue global scratch scheduling/reentrancy; captured named cancellation;
context serialization/raw references; registry reload/teardown; receiver rebuild
after extender replacement; revolute step/target/sentinel producers;
light79F5E0/79F870/79FE90, shield8A/8B/982A10 and structure effects;
sound-source lifetime beyond linked aliases. These remain concrete static leads.
