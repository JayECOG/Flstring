# Issue Triage Playbook

## Goals
- Reproduce quickly.
- Label consistently.
- Prioritize by user impact + risk.
- Keep API/ABI stability explicit.

## Labels
- `bug`, `performance`, `api`, `compatibility`, `question`, `needs-triage`, `blocked`, `good first issue`.

## Severity rubric
- **S0 Critical**: data loss, UB/security, widespread crash.
- **S1 High**: major feature unusable, severe perf regression.
- **S2 Medium**: workaround exists, moderate impact.
- **S3 Low**: edge case, docs/readability.

## Priority rubric
- **P0**: immediate patch release needed.
- **P1**: next release.
- **P2**: planned backlog.
- **P3**: nice to have.

## Triage checklist
1. Confirm issue type (bug/perf/api/question).
2. Verify repro details and environment.
3. Reproduce on `main`.
4. Estimate severity + priority.
5. Assign owner and target milestone.
6. Link benchmark/report templates where needed.

## Library-specific risk checks
- API parity with `std::string` behavior.
- ABI risk (layout, inline contract, symbol changes).
- Allocation behavior changes in hotpaths.
- Cross-platform build behavior (MSVC/Clang/GCC).

## Exit criteria
- Repro status documented.
- Labels + severity/priority set.
- Next action clearly assigned.
