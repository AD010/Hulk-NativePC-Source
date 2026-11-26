---
name: hulk-resume-and-release
description: Resume parallel Hulk development safely, implement missing runtime paths and enforce source, compile, gameplay and Release acceptance.
---
Read CLAUDE.md and AGENTS.md. Start at actual main HEAD, inspect recent commits
and changed files, then select the smallest complete missing runtime behavior.
Re-use existing assets, tests and patch plan. Avoid overlapping edits and
publish only fast-forward commits against the inspected parent; on conflict
re-read the new HEAD. Record exact evidence and remaining blockers, not guessed
percentages. Run targeted regressions, then applicable source gates and
real compilation where available. Preserve the single manual Release-only
workflow and never dispatch without the owner's explicit approval. A ZIP,
test success and an actual independent gameplay frame are distinct outcomes.
