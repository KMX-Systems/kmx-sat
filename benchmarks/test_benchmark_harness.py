import tempfile
import unittest
from pathlib import Path

import run_benchmarks
import run_solver_comparison


class BenchmarkHarnessTests(unittest.TestCase):
    def test_representative_run_excludes_timeout_samples(self):
        runs = [
            {"elapsed_ms": 10.0, "timed_out": False, "status": "SATISFIABLE", "elapsed_ns": 10_000_000},
            {"elapsed_ms": 11.0, "timed_out": False, "status": "SATISFIABLE", "elapsed_ns": 11_000_000},
            {"elapsed_ms": 30.0, "timed_out": True, "status": "UNKNOWN", "elapsed_ns": 30_000_000},
        ]

        representative = run_benchmarks.representative_run(runs)

        self.assertEqual(representative["elapsed_ms_median"], 10.5)
        self.assertEqual(representative["elapsed_ms_min"], 10.0)
        self.assertEqual(representative["completed_run_count"], 2)
        self.assertEqual(representative["timed_out_run_count"], 1)

    def test_representative_run_handles_all_timeouts(self):
        runs = [{"elapsed_ms": 5.0, "timed_out": True, "status": "UNKNOWN", "elapsed_ns": 5_000_000}]

        representative = run_benchmarks.representative_run(runs)

        self.assertEqual(representative["completed_run_count"], 0)
        self.assertEqual(representative["timed_out_run_count"], 1)
        self.assertEqual(representative["status"], "UNKNOWN")

    def test_solver_comparison_accepts_standard_status_exit_codes(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "solver.py"
            executable.write_text(
                "#!/usr/bin/env python3\nprint('s SATISFIABLE')\nraise SystemExit(10)\n",
                encoding="utf-8",
            )
            executable.chmod(0o755)

            status, _, timed_out, exit_code = run_solver_comparison.run_solver(
                executable, Path("fixture.cnf"), 2.0
            )

        self.assertEqual(status, "SATISFIABLE")
        self.assertFalse(timed_out)
        self.assertEqual(exit_code, 10)

    def test_solver_comparison_timeout_is_classified(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "solver.py"
            executable.write_text(
                "#!/usr/bin/env python3\nimport time\ntime.sleep(10)\n",
                encoding="utf-8",
            )
            executable.chmod(0o755)

            status, _, timed_out, exit_code = run_solver_comparison.run_solver(
                executable, Path("fixture.cnf"), 0.05
            )

        self.assertEqual(status, "UNKNOWN")
        self.assertTrue(timed_out)
        self.assertEqual(exit_code, -1)

    def test_solver_comparison_rejects_abnormal_exit(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "solver.py"
            executable.write_text(
                "#!/usr/bin/env python3\nprint('s SATISFIABLE')\nraise SystemExit(1)\n",
                encoding="utf-8",
            )
            executable.chmod(0o755)

            status, _, timed_out, exit_code = run_solver_comparison.run_solver(
                executable, Path("fixture.cnf"), 2.0
            )

        self.assertEqual(status, "SATISFIABLE")
        self.assertFalse(timed_out)
        self.assertEqual(exit_code, 1)

    def test_summarize_empty_samples_is_safe(self):
        self.assertEqual(run_solver_comparison.summarize([]), (0.0, 0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
