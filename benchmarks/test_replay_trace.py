import json
import os
import tempfile
import unittest
from pathlib import Path

from replay_trace import replay, validate


class ReplayTraceStatefulTests(unittest.TestCase):
    def test_validate_rejects_malformed_and_incompatible_traces(self):
        cases = {
            "malformed-json": '{"schema":1,"seed":1,"variable_count":1,"kind":"header"}\n{"kind":',
            "truncated-header": '{"schema":1,"seed":1,"variable_count":1,"kind":"header"',
            "schema-version": json.dumps({"schema": 2, "seed": 1, "variable_count": 1, "kind": "header"}),
            "unknown-operation": "\n".join(
                [
                    json.dumps({"schema": 1, "seed": 1, "variable_count": 1, "kind": "header"}),
                    json.dumps({"kind": "unknown"}),
                ]
            ),
            "malformed-literals": "\n".join(
                [
                    json.dumps({"schema": 1, "seed": 1, "variable_count": 1, "kind": "header"}),
                    json.dumps({"kind": "add_clause", "literals": [0]}),
                ]
            ),
            "invalid-limits": "\n".join(
                [
                    json.dumps({"schema": 1, "seed": 1, "variable_count": 1, "kind": "header"}),
                    json.dumps({"kind": "solve", "conflict_limit": -1, "decision_limit": 0}),
                ]
            ),
        }
        with tempfile.TemporaryDirectory() as temporary:
            for name, contents in cases.items():
                with self.subTest(name=name):
                    path = Path(temporary) / f"{name}.jsonl"
                    path.write_text(contents + "\n", encoding="utf-8")
                    self.assertNotEqual(validate(path), [])

    def test_validate_accepts_stateful_operations(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "stateful.jsonl"
            path.write_text(
                "\n".join(
                    [
                        json.dumps({"schema": 1, "seed": 9, "variable_count": 2, "kind": "header"}),
                        json.dumps({"kind": "set_option", "option": "chb_enabled", "value": 1}),
                        json.dumps({"kind": "add_clause", "literals": [1, -2]}),
                        json.dumps({"kind": "solve", "conflict_limit": 0, "decision_limit": 0}),
                        json.dumps({"kind": "value_of", "variable": 1}),
                        json.dumps({"kind": "failed", "literal": 1}),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            self.assertEqual(validate(path), [])

    def test_replay_tracks_value_and_failed_queries(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "trace.jsonl"
            path.write_text(
                "\n".join(
                    [
                        json.dumps({"schema": 1, "seed": 7, "variable_count": 1, "kind": "header"}),
                        json.dumps({"kind": "add_clause", "literals": [1]}),
                        json.dumps({"kind": "solve", "conflict_limit": 0, "decision_limit": 0}),
                        json.dumps({"kind": "value_of", "variable": 1}),
                        json.dumps({"kind": "failed", "literal": 1}),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            statuses, value_results, failed_results = replay(
                path,
                "./source/build/default/kmx-sat-cdcl-test.5619adea/kmx-sat-cdcl-test {instance} {assumptions} --decision-limit {decision_limit} --conflict-limit {conflict_limit}",
            )
            self.assertGreaterEqual(len(statuses), 1)
            self.assertEqual(len(value_results), 1)
            self.assertEqual(len(failed_results), 1)


if __name__ == "__main__":
    unittest.main()
