# Actor FSM routing and completion — build25492057

Static review continuing equipment-actions. RVAs and field offsets are
hexadecimal; virtual slots are decimal. No runtime verification or completeness
claim. The focused exports are frozen with the validated manifest.

## Requests and authored fields

Parser wrapper7FF7D0 installs registry157E058 and dispatches7FF8B0. Matching
editor wrapper6816E0 installs13E5728 and dispatches6817C0. Exact token initializers
bind priority8C, exit92, request94, interrupt95, duration96, switch9B.
For a Request, the parser appends an F8-byte record and writes name8, priorityCC,
exit-name68, interruptC9, durationD0, switch indexD4 and reverseD8. Reverse is true
when the authored switch direction integer equals0. Duration is an int32 property;
it is **not** the DC completion value. Tick7FB6A0 establishes the following
expiry rules, measured in calls to that tick rather than seconds:

| Request mode at0 | Per-update behavior |
|---|---|
|0|No expiry in this loop; consumed immediately when ready by7FDF00.|
|1|Consumes an active request if E0 is0, then clears E0. Request API mode1 writes E0=1, so continuing to poll keeps it alive.|
|2|Increments E0 and consumes when E0>D0. API mode2 resets E0=0.|
|3|No expiry in this loop; protectionC8 is set when started by API mode3.|

7FBC80 (`eFsm::MakeRequest`) supplies DC: it delivers event6 with any old positive
DC, stores the caller's new completion subkind and starts an inactive request in
mode0. Drop7FDDE0 delivers that subkind and clears DC. This callback mechanism is
distinct from the authored duration and from the named blend-completion payload.

Constructor804350 defaults mode0=1, priorityCC/durationD0=0, C8/C9=0, DC/E0=0,
readyE4/activeE5=0, ownerE8=`none`, flagsF0=2, switchD4=-1 and reverseD8=0.
This binds the installed priorities to the arbitration previously mapped: dead50,
throwoff48, knock-down47, equipment holds45, inventory43, stance40, reload35,
move30, fire25, aim20 (authored decimal values). It is an animation-request
precedence scheme, not the brain's target desirability score.

Comparator800E30 is a16-byte leaf not recognized as a Ghidra function in the
current database. Direct LLVM decoding is `return int32(a.CC)>int32(b.CC)`.
803630 passes the pointed-to records to it and sorts higher priorities first.
It uses insertion sort for up to32 pointers and partition/heap fallbacks above
that, so global equal-priority stability is not established. Preserve this leaf
as exact bytes/manual disassembly evidence, not a fabricated Ghidra entry.

## Graph search and readiness

State records have strideA28.7FCEE0 resets every state's search distanceA20 and
predecessorA24 to-1, sets the start distance0, and searches a FIFO of indices.
Each popped state's tag list3A8/3B0 is checked for the request name. On success it
appends indices from target backward through predecessor links, excluding the
start; output is consumed from its end. A start already bearing the requested
tag succeeds with an empty output.

Neighbors come from70/78 and are traversed in reverse stored order. A neighbor
must have flags68 bit0 set and its condition listA08/A10 must be empty or contain
the requested name. It is queued only if unseen or reached with a shorter
integer distance. This is a shortest-edge-count search under those filters,
not a shortest-animation-time search. It mutates shared state-record scratch
fields and is not a passive query.801E00 appends the state's index, not a pointer;
805AC0 appends the start index to the FIFO.

7FD210 exits the current state, copies the reversed path to A00/A08, pops its
last index and calls7FD330. It assumes a nonempty path; readiness/phase/caller
invariants must explain cases where7FCEE0 would return an empty one.7FD330 sets
A18=1, phase9F8=0 and current9F0=&states[index].7FD4A0 sets phase3 then runs the
phase dispatcher immediately.

7FEE10 marks eligible requests ready only while phase9F8==2. It scans active,
unready records whose name is in the current state's tags. ProtectedC8 requests
also require A18 clear. Effective owner is `none` with flagsF0=0, otherwise E8;
FSM D50 must match either effective owner or E8. It then assigns E8 to D50 and
calls7FDF00. It stops if that callback changes phase away from2. Arbitration
7FE6E0 runs this refresh before selecting a new route, subject to A18.

## Phase completion and handlers

The debug formatter7FC6E0 indexes six native names from1480D58:0=`do_enter`,
1=`entering`,2=`inside`,3=`do_exit`,4=`exiting`,5=`outside`.

7FD540 phase0 calls7FD760 to enter the current state then writes phase1.
Phases0/1 become phase2 only once the context name vector328/330 is empty.
Phase3 sets reentrancy byte672, clears that vector, invokes handlerE88 slot3 with
phase-1 and current state name8, clears672 and state-switch marker9D0, and goes4.
Phase4 becomes5 only when the same vector is empty. Other phases passed to this
routine emit a native `wrong phase` diagnostic.

7FB6A0 advances phases while A18 is set. At phase2 with no queued state and no
default transit88, it clears A18 and runs arbitration. Otherwise it refreshes
requests then goes to phase3. At phase5 it pops the route, or uses transit88;
missing both reaches `next state not found`. It sets FSM671=1 after phase dispatch.
No frame-time decrement occurs in these request counters. Entry/exit handlers
control which operations actually require subsequent updates.

7FD760 clears the active completion context, marks changingState672, and compares
each requested split token with the remembered part atA20. Changed parts call
handler slot3 once for old-token exit and once for new-token entry; then the new
split vector is copied. It also calls slot3 with part=-1 and the whole state name.
On entry, a nonempty saved context340 replaces context10 and is cleared; this
preserves pending names across the direct switch-translation path. The function
then clears672 and9D0. These are names/part transitions, not final pose matrices.

Human handler event9605C0 receives entity event6. Subkind3 removes a supplied name
from the FSM completion contexts through7FCAD0; if matching its separately active
name3B8 it clears active3B0, that name and resets scalar418 to1. Subkind6 decrements
handler560 and removes the supplied completion name. GEM7E7D30 explicitly calls
this field `activeBlends` and asserts it remains nonnegative after decrement;
the game lacks that check. This closes a decrement for the idle-prepare/idle gate
mapped in human-geometry. Subkinds4/5 refresh deadlines
3A0/3A8 when handler30 masks700/800 permit. Other event branches remain separately
scoped.7FCAD0 calls805150 on both tag contexts10 and340; it is not cancellation
of a named request record.7FCAC0 adds a completion name to context10 using124830,
which inserts only if absent.805150 removes the first matching name and compacts.

Human961360 schedules a blend only for nonempty names, handler30 bit1 clear and
FSM671 set; otherwise it creates/applies/disposes an animation directly without
incrementing activeBlends. In the blend path,564 permits adding the barrier name,
560 increments, and queue430 receives an animation request with callback subkind6.
961480 uses queue470 and increments without the former gate;961500 increments
and optionally adds the barrier but has no direct code caller in this export.
The blend argument is writable scale14776C8 times0.2; its file-initial value is
1000, giving200 native time units. Exact clock conversion
and all blend subclasses remain open.

Queue helper962B50 appends a68-byte name/status record (status60=0), resolves an
optional existing animation's slot, and tail-jumps5CDD20→5CD380. Assembly preserves
the fifth argument (event subkind) across that tail call and passes the duration
in XMM3; the decompiler's shorter prototype hides both details.5CD380 reuses or
creates a blend controller, stores its event subkind at8 and invokes slot5.
No controller with a callback-producing mode triggers5CDFE0 immediately. A slot5
return0 likewise emits entity event6 with subkind and definition-name pointer,
then clears the callback code. This provides a synchronous completion path;
normal animation quant/completion, disposal and cancellation are continuing leads.

7FB570 replaces handlerE88, deletes a prior non-default handler via slot1, falls
back to singleton1480D28 for null, and binds a non-default handler's raw owner28
before calling slot0 if needed.7FF5A0 resource-copy duplicates states, requests,
subsets and switch tables, recreates the handler from its registry name and
performs the same owner binding. It does not simply share the original handler.
Full actor/FSM replacement and pending-callback lifetime remain open.

## Parser graph declarations

State8B enables FSM670, finds/creates a state and marks its flags68 bit1 as declared.
Tag8D appends the native tag; transit8E writes default-next88 and also adds a
neighbor; to8F adds an outgoing neighbor; from90 adds this state to another's
neighbors. fromTagged91 builds a tag mask and adds this state as a neighbor to
matching existing states. The tag matcher556F20 requires **all** names in the
mask (an empty mask matches); this applies to fromTagged and subset rules.
Condition9D appends the allowed-request name consumed
by the search. Start93 stores its state index atFSM674.

Ensure-state7FF0D0 creates forward references with bit0 enabled, bit1 undeclared,
transit88=-1 and default split tokens fromFSM6A0. A state contains index0,name8,
flags68,neighbors70,transit88,tags90,split3C0,conditions6F0,searchA20/A24.
State/split/condition structures use330-byte tag containers; their name vectors
are at+318. Tags at3A8/3B0 therefore belong to the state's tag container90.

Global Split9A sets count698 and appends that many`linked` default part tokens;
a state Split replaces its part vector and reads exactly that count. A subset
is78 bytes (name0,rules60 vector); each338-byte rule has enable byte0 and mask8.
`from` clones an already declared subset's rules; new rules append.7FB950 finds
the subset7FCB10, stores E80, starts each state's enabled bit at1, then overwrites
it for each matching rule. **Last matching rule wins**. It does not prune current
or queued states here; route search tests enabled bits on neighbors.

Switch records are80 bytes (name0,index60,pairs68).7FF300 finds or creates them.
Authored switch/split generates pairs by matching the chosen part's from/to tokens
and equality of all other parts, taking the first target match. Explicit link
appends a pair of state indices. RequestD8 selects pair direction at runtime.
The final parser validation reports undeclared references. Except for states
`dead` and`common`, it checks outgoing links and exactly one neighbor carrying
the **`dying`** tag (the diagnostic describes a transition to `dead`). Except for
`boarding`, it checks reachability from configured Start674. Initializers
58260/581A0/58170 bind those exceptions; literal10DEBB8 and assembly7FF9D8 bind
the actual tag.7FCC70 checks bare graph reachability using A20
scratch, ignoring runtime enabled/condition filters. It is not a request
feasibility predicate.

## Equipment classification and subset selection

Human95FAD0 receives a tag container, sets30 bit15, clears overrides420/428, derives
class424 with9610B0 and modifier42C from a six-token table, refreshes flag30 bit3,
selects the FSM subset9615D0 and calls animation refresh961710.9610B0 checks the
following class rows in order, requiring both tags when the second is nonempty;
no match returns0. Initializers69C90 and its source-token initializers bind names.

| Value (decimal) | Required tags |
|---|---|
|0|nothing|
|1|rocketlauncher + weapon|
|2|mgun + weapon|
|3|shortsmg + weapon|
|4|smg + weapon|
|5|rifle + weapon|
|6|pistol + weapon|
|7|flame_thrower + weapon|
|8|smallitemin1hand|
|9|itemin1hand|
|10|itemin2hands|
|11|human|
|12|flag|

Modifier42C is the first matching index among nothing,grenade,knife,chicken,axe,
binocular (initializer69830), defaulting0.9615D0 selects default for classes0/8
with modifier0, with_weapon for classes0/8 with other modifiers or classes2–7,
with_bazooka for class1, and with_item for classes9–12. The last group calls
chassis slot48; classes11/12 also set handler30 bit0. It does not choose `on_board`
here. Installed subset.inc shows last-rule overrides allow open-with-item states,
while equipment can disable aiming, stance, repair/revive or looking routes.
Complete callers supplying those tag containers and animation refresh remain open.

Order::eFsmRequest factory8D4080 allocates98 with name38. Its Execute8D3EB0 emits
command2F on first phase when synchronization permits, then polls API mode1 with
flags2/owner`none` until ready. Cleanup phase returns success without a local
explicit request cancel; mode1's tick-latch expiry is relevant. Base-order return
codes/lifecycle were mapped in earlier passes. Command2F decoding is a next lead.

## Editor differences and remaining work

Editor67D6B0 tick asserts Valid();67F3F0 state switch requires !changingState,
phase OUTSIDE and nonnegative index.67F590/67F700 assert callbacks preserve current.
Editor67FF90 direct interrupt/switch paths also assert immediate exit reached
OUTSIDE and the chosen path is nonempty. The game omits these checks. They explain
intended caller/handler invariants, not runtime guarantees or proof that every
data-driven path respects them. Editor67EF90 additionally checks a valid start
index and nonempty tag. The reviewed graph, expiry, readiness, request parser,
subset and handler contracts agree on their operative fields.

Open static work includes native animation controllers and their quant/cancel
events, all twelve human handler dispatch families960970, equipment tag producers,
FSM serialization and lifetime during actor/handler replacement, command2F,
the full expanded installed state graph (including define/args macros), tick
scheduling at its callers, and behavior that can change enabled/current states.
This is bounded FSM progress, not exhaustion of the AI/entity frontier.

The manifest records85 game contracts/slices,19 selected editor counterparts,
74 fields and9138 instruction locations. Discovery exports contain132 game and80
editor functions; those totals are not semantic coverage counts. Structural
checks passed for binary/export hashes, exact candidate entries,10 RTTI tables,
69 byte sequences, the6-name phase pointer table, one read-only float and8
installed resource hashes. Independent LLVM disassembly matched5684 instruction
starts across34 functions after LOCK-prefix normalization. Both16-byte priority
comparators were separately decoded by LLVM and have identical checked bytes.
No executable was launched, patched or injected into during these checks.
