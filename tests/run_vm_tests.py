"""Parallel VM test runner: pre-build once, fan out QEMU runs.

Usage: python -m tests.run_vm_tests [--workers N] [--json]

Builds all solution .ko files sequentially (cache-friendly), then
runs each VM check in parallel (I/O-bound QEMU boots).
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from engine.catalog import Catalog  # noqa: E402
from tests.vm import VM, build_module, clean_module, REPO, VMError  # noqa: E402


class _LaxCatalog(Catalog):
    def validate(self):
        pass


@dataclass
class VMJob:
    exercise_id: str
    case_id: str
    ko: Path
    check: Path
    check_args: tuple[str, ...]
    tools: tuple[str, ...]
    timeout: int
    bare: bool
    network: bool
    build_vars: dict[str, str]


@dataclass
class VMJobResult:
    job: VMJob
    passed: bool
    output: str


def discover_jobs(
    catalog: _LaxCatalog,
    build_root: Path,
    on_build: None | callable = None,
) -> list[VMJob]:
    """Materialize solutions, build .ko files, return runnable jobs."""
    tool_registry = {tool.id: tool.binary for tool in catalog.tools}
    solution_cache: dict[str, Path] = {}
    jobs: list[VMJob] = []
    build_index = 0
    exercises_with_cases = [
        item for item in catalog.exercises if item.verification.cases
    ]
    build_total = len(exercises_with_cases)

    for item in exercises_with_cases:
        cases = item.verification.cases
        if not cases:
            continue

        timeout = item.verification.timeout_seconds
        tool_binaries = [tool_registry[tid] for tid in item.verification.tools]
        bare = item.stage == "module-core"

        if item.id not in solution_cache:
            build_index += 1
            if on_build:
                on_build(item.id, build_index, build_total)
            solution_cache[item.id] = catalog.materialize_solution(
                item, build_root,
            )
        src = solution_cache[item.id]

        for case in cases:
            if not case.check:
                continue

            needs_network = "socket-connect" in case.check
            build_vars = dict(case.build_variables)

            if case.expect_build == "fail":
                continue

            clean_module(src)
            try:
                ko = build_module(src, variables=build_vars)
            except (subprocess.CalledProcessError, VMError):
                jobs.append(VMJob(
                    exercise_id=item.id, case_id=case.id,
                    ko=Path("/dev/null"), check=REPO / case.check,
                    check_args=case.check_arguments,
                    tools=tuple(tool_binaries), timeout=timeout,
                    bare=bare, network=needs_network,
                    build_vars=build_vars,
                ))
                continue

            ko_copy = build_root / f"{item.id}_{case.id}.ko"
            shutil.copy2(ko, ko_copy)

            jobs.append(VMJob(
                exercise_id=item.id, case_id=case.id,
                ko=ko_copy, check=REPO / case.check,
                check_args=case.check_arguments,
                tools=tuple(tool_binaries), timeout=timeout,
                bare=bare, network=needs_network,
                build_vars=build_vars,
            ))

    return jobs


def run_job(job: VMJob) -> VMJobResult:
    """Boot QEMU and run one check."""
    if str(job.ko) == "/dev/null":
        return VMJobResult(job, False, "build failed")
    vm = VM()
    try:
        result = vm.run_check(
            job.check, job.ko, timeout=job.timeout,
            check_args=job.check_args, tools=job.tools,
            bare=job.bare, network=job.network,
        )
        return VMJobResult(job, result.returncode == 0, result.output)
    except VMError as exc:
        return VMJobResult(job, False, str(exc))


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--workers", type=int, default=0)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    workers = args.workers
    if workers <= 0:
        try:
            total_mb = (
                os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
            ) // (1024 * 1024)
            workers = max(2, min(total_mb // 4, 4096) // 256)
        except (ValueError, OSError):
            workers = 2

    catalog = _LaxCatalog(REPO)
    build_root = Path(tempfile.mkdtemp(prefix="rklings-vm-"))

    if not args.json:
        sys.stderr.write(f"Building solutions...\n")
        sys.stderr.flush()

    jobs = discover_jobs(catalog, build_root)

    if not args.json:
        sys.stderr.write(f"Running {len(jobs)} VM checks, {workers} workers\n")
        sys.stderr.flush()

    passed = 0
    failed_results: list[VMJobResult] = []

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(run_job, job): job for job in jobs}
        done = 0
        for future in as_completed(futures):
            result = future.result()
            done += 1
            if result.passed:
                passed += 1
            else:
                failed_results.append(result)
            if not args.json:
                sys.stderr.write(f"\r{done}/{len(jobs)}")
                sys.stderr.flush()

    if not args.json:
        sys.stderr.write("\n")

    if args.json:
        output = {
            "total": len(jobs),
            "passed": passed,
            "failed": len(failed_results),
            "failures": [
                {
                    "exercise": r.job.exercise_id,
                    "case": r.job.case_id,
                    "output": r.output[-500:],
                }
                for r in failed_results
            ],
        }
        sys.stdout.write(json.dumps(output))
    else:
        if failed_results:
            for r in failed_results:
                sys.stderr.write(
                    f"\nFAIL: {r.job.exercise_id}::{r.job.case_id}\n"
                )
                sys.stderr.write(r.output[-500:])
                sys.stderr.write("\n")

    return 0 if not failed_results else 1


if __name__ == "__main__":
    raise SystemExit(main())
