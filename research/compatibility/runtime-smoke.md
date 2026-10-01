# Controlled runtime scenarios — pending adapter implementation

These are acceptance scenarios, not executed tests or proof of a callable ABI.
No runtime adapter has yet established the native call signatures, object
ownership, thread affinity, or lifetime rules. The static auditor always emits
`runtime_allowed: false`, including when every required static check passes.

Before implementing these scenarios, establish the adapter's ABI/lifecycle
contracts on one exact executable. Record the binary and resource fingerprints,
adapter revision, scenario/save revision, active mods/configuration, and any
random seed or timing controls. Run on a disposable local single-player/editor
scenario with one friendly human, one target, and one cover location. Record
actual observations and failures; a skipped scenario is not a pass.

| Scenario | Trigger and observations | Acceptance criteria |
|---|---|---|
| Read-only identity | Enumerate the fixture actors through the verified adapter; observe actor/brain identity and secondary interfaces over several ticks | Handles identify the same live actors; RTTI/subobject relationships match the profile; no invalid reads |
| Update callback | Observe ordinary engine updates and a normal brain-update order while recording thread, actor, and call depth | Expected callback path and pending flag transitions are observed; original engine progression continues; no reentrant callback surprises |
| Ratio access | Load explicit advance/retreat ratio values in the fixture configuration; read them through the adapter | Complete-object reads agree with the effective property values; the secondary-this adjustment is demonstrated |
| Movement modes | Use supported engine controls to change movement mode and speed; inspect through the adapter, then exercise its verified setter | Named modes agree with enum values and bit fields; unrelated bits are preserved at the controlled observation point; expected side effects occur |
| Lifetime/teardown | Remove an actor, end the mission, and load the fixture again | Stale handles are rejected before native access; callbacks stop for destroyed objects; no previous-scene pointers survive reload |
| Cover order, when supported | Submit one cover order, then exercise accepted, rejected, and cancelled cases | Actor follows the expected order; allocation/ownership/cleanup matches the verified contract; rejection and cancellation do not leak or double-release objects |

Store results separately from static profiles, keyed by the exact executable
hash. Preserve failed and unexecuted scenarios. Supporting a new runtime should
require both reviewed static mappings and the relevant observed scenario passes;
the update auditor's exit code alone is insufficient.
