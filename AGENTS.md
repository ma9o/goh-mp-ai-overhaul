# Agent instructions

Research plan and scope: `research/notes/decision-frontier-25492057.md`. Read
it before starting any work. Its scope rule and working rules govern all
static mapping here. They take precedence over
`research/notes/static-frontier-25492057.md` (now a historical index) and over
the README's game/editor pairing rule: map the game binary, and use the editor
only when one of its assertions answers an open item.

- Work one open item at a time, in order. Add a new item only when it names the
  catalog row and blank it fills; park everything else as one line.
- Keep existing work. Never delete, rewrite, move or "clean up" existing notes,
  manifests, TSVs, scripts, tests or the old frontier, parked or not.
- Static analysis only. Do not launch or modify the game or the editor.
- Stage files by path, only those you changed for the item. Start commit
  messages with the item ID. Do not push.
