"""Native CLI lifetime-capacity admission and archive-clock fixtures."""
import os
import unittest

import tests_cli


class CapacityTests(tests_cli.CLITests):
    def reject_unchanged(self, planned, reserve, gen=1):
        before = self.path.read_bytes()
        ledgers = {p.name: p.read_bytes() for p in self.session.glob('.generations-*')}
        result = self.call_bad('arm', self.path, gen, self.identity, planned, reserve)
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual({p.name: p.read_bytes() for p in self.session.glob('.generations-*')}, ledgers)
        return result

    def test_capacity_exact_fit_and_no_reset(self):
        self.patch(24, tests_cli.CAPACITY - 10)
        self.assertEqual(self.one(self.call_ok('inspect', self.path))['remaining_capacity'], 10)
        armed = self.arm(planned=7, reserve=3)
        self.assertEqual(armed['remaining_capacity_at_admission'], 10)
        self.assertEqual(armed['planned_records'], 7)
        self.assertEqual(armed['tail_reserve'], 3)
        self.assertFalse(armed['budget_enforced_during_capture'])
        self.assertEqual(self.header()[3], tests_cli.CAPACITY - 10)
        self.stop()
        self.arm(2, planned=10, reserve=0)
        self.assertEqual(self.header()[3], tests_cli.CAPACITY - 10)
        self.stop(2)

    def test_rejected_capacity_does_not_consume_generation_or_change_ledger(self):
        self.arm()
        self.stop()
        self.patch(24, tests_cli.CAPACITY - 10)
        self.reject_unchanged(7, 4, gen=2)
        self.reject_unchanged(11, 0, gen=2)
        self.arm(2, planned=7, reserve=3)
        self.stop(2)

    def test_full_or_overflowed_ring_never_rearms(self):
        for next_reserved in (tests_cli.CAPACITY, tests_cli.CAPACITY + 1, 2**64 - 1):
            with self.subTest(next_reserved=next_reserved):
                self.patch(24, next_reserved)
                self.assertEqual(self.one(self.call_ok('inspect', self.path))['remaining_capacity'], 0)
                self.reject_unchanged(1, 0)

    def test_budget_numeric_validation_and_overflow(self):
        for value in ('', '-1', '+1', ' 1', '1x', '1.0', str(2**64), '9' * 100):
            with self.subTest(value=value):
                self.reject_unchanged(value, 0)
                self.reject_unchanged(1, value)
        self.reject_unchanged(0, 0)
        self.reject_unchanged(1, 2**64 - 1)
        self.reject_unchanged(2**64 - 1, 1)
        self.call_bad('arm', self.path, 1, self.identity, code=2)
        self.call_bad('arm', self.path, 1, self.identity, 10, code=2)
        self.arm(planned=1, reserve=0)
        self.stop()

    def test_binary_archive_clocks_preserve_flags_and_unknown_type(self):
        self.arm()
        rows = [(17, 1), (17, 2), (19, 0), (18, 0)]
        with self.path.open('r+b') as handle:
            for index, (kind, flags) in enumerate(rows):
                record = tests_cli.EVENT.pack(100, 250, 11, kind, 2, flags, 1, 0, 0, index + 1)
                os.pwrite(handle.fileno(), record, 4096 + index * tests_cli.EVENT.size)
        self.patch(24, len(rows))
        self.stop()
        decoded = self.rows(self.call_ok('decode', self.path, 1, self.identity))
        for row, (kind, flags) in zip(decoded[:-1], rows):
            self.assertEqual(row['flags'], flags)
            self.assertEqual(row['clock_domain'], 'unknown' if kind == 18 else 'cpu_steady_ns')
            self.assertEqual(row['duration_ns'], None if kind == 18 else 150)
        # Use the real binary's NDJSON, not hand-authored analyzer input.
        import analyze
        analysis = analyze.analyze(decoded)
        self.assertIsInstance(analysis, dict)


if __name__ == '__main__':
    unittest.main()
