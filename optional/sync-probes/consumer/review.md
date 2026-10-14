# Independent consumer review

Read-only reviewer inspected `analyze.py`, `probe_semantics.hpp` and native
decoder annotations. No blocking join or semantics flaw was found. Six
additional in-memory adversarial observations passed: later reason timestamp
supplies only a label, valid+malformed duplicate reason is ambiguous, query
pointer numerically equal to sequence never joins, wait/current and historical
chunk frames stay separate, a rejected duplicate chunk cannot supply a wait
join, and an orphan boundary wait retains only its own observed duration.

Native uint32 widths and new probe flags are validated. Type 33 uses exact
selected generation/queue/sequence/frame; duplicate or invalid markers refuse
attribution. Types 30/32 preserve separate call frame and type 31 has no chunk
sequence. No reviewer file edits or live runtime work occurred. Producer helper
and PE integration verification belong to the renderer owner.
