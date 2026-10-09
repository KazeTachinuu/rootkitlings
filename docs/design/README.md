# Internal curriculum design

These documents record the proposed next curriculum and the research behind it.
They are implementation inputs, not learner documentation for the currently released route.
The target has an unscored authorization and containment setup gate followed by 32 lessons, four milestones, and one capstone: 37 scored checkpoints.

- `curriculum-knowledge-graph-draft.md` inventories the complete knowledge dependency graph and primary sources.
- `curriculum-migration-map.md` maps every current exercise to the proposed cumulative curriculum.
- `target-catalog-v3.md` defines the exact target lessons, milestones, capstone, and elective tracks.
- `engine-v3-requirements.md` records the engine changes required before route adoption.
- `repository-cutover.md` fixes the final root layout and the safe atomic removal order for the live v2 trees.
- `stage-01-architecture.md` fixes the cumulative source, ownership, failure, tracing, and snapshot contracts.
- `stage-01-implementation.md` defines the setup-gate acceptance contract and tracks the Stage 1 implementation slice.
- `stage-02-implementation.md` records the validated control, scheduling, resident-thread, and M00 integration slice.
- `stage-03-architecture.md` fixes the M01 integration, event-ring, publication, and lifecycle contracts.

Until the migration is complete, the repository root README and the generated catalog describe the implemented course.
