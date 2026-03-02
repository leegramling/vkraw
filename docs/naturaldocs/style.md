# Natural Docs Style Conventions

## Comment Templates

- File header:
  - Purpose of file.
  - Ownership boundary (what it should and should not contain).
- Class header:
  - Responsibility.
  - Key dependencies.
  - Lifetime/ownership notes.
- Method header:
  - Inputs/outputs.
  - Side effects.
  - Threading/frame-loop expectations when relevant.

## Mermaid Conventions

- Use fenced blocks with `mermaid`.
- Keep diagrams small and local to the file/module they describe.
- Prefer one primary flow per diagram.
- Use stable node names that match class or method names.

## Placement Rules

- Architecture-level diagrams go in top-level docs (for example `core.md`).
- Local behavior diagrams may be embedded in source comments near entry methods.
- Avoid duplicating the same diagram in multiple places; link to canonical docs.
