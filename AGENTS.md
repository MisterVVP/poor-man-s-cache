# AGENTS.md

## Project philosophy
- Build a high-performance, minimalist cache server focused on Linux deployments.
- Prioritize performance over readability/pattern purity when there is a trade-off; pragmatic C-like solutions are acceptable in hot paths.
- Keep external dependencies minimal (exceptions are acceptable for tests and non-core features such as metrics).
- Validate input at the network boundary; avoid redundant defensive checks deep in internal core paths.

## Non-negotiable engineering requirements
- Prefer **O(1)** algorithmic complexity in request-path operations whenever possible.
- Any change that risks regressions in complexity or throughput must be redesigned (for example, avoid full-table scans in frequently executed code paths).
- Unit test coverage is mandatory for new or changed behavior.
- Verify all changes before committing.
- Performance must not degrade: run python functional/performance tests that report RPS as part of verification.
