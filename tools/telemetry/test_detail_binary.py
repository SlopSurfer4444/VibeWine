"""Additive event clocks/statuses survive the actual immutable binary decoder."""
import os,unittest
import tests_cli
from analyze import analyze

class DetailBinaryTests(unittest.TestCase):
    def test_retained_generation10_then9_excludes_history_and_checks_current(self):
        for malformed in (False,True):
            fixture=tests_cli.CLITests();fixture.setUp();self.addCleanup(fixture.doCleanups)
            def append(es,gen,offset):
                with fixture.path.open('r+b') as handle:
                    for index,(t,stamp,obj,p0,p1,flags) in enumerate(es,offset):
                        end=stamp-1 if malformed and gen==9 and t==84 and obj==400 else stamp
                        os.pwrite(handle.fileno(),tests_cli.EVENT.pack(stamp,end,obj,t,2,flags,gen,p0,p1,index+1),4096+index*64)
                fixture.patch(24,offset+len(es))
            fixture.arm(10)
            append([(80,100,300,301,1,2),(81,110,300,301,0,2),(84,120,300,301,0,0)],10,0)
            fixture.stop(10);fixture.arm(9)
            current=[(80,200,400,301,1,2),(81,210,400,301,0,2),(82,220,400,500,301,0),(80,230,500,301,1,2),(81,240,500,301,0,2),(83,249,400,500,301,0),(84,250,500,301,0,0),(81,260,400,301,0,2),(84,270,400,301,0,0)]
            append(current,9,3);fixture.stop(9)
            result=fixture.call('decode',fixture.path,9,fixture.identity);self.assertEqual(result.returncode,0,result.stderr)
            decoded=fixture.rows(result);self.assertEqual(decoded[-1]['wrong_generation'],3)
            d=analyze(decoded)['detailed_probes'];self.assertEqual(d['historical_generation_records_excluded'],3)
            self.assertEqual(d['capture_usable'],not malformed)
            self.assertEqual({t['task'] for t in d['tasks']},{400,500})
            if not malformed:
                self.assertEqual(d['tasks'][0]['dependency_waits'][0]['dependency_join'],'observed_ready_task')
                self.assertEqual(d['tasks'][0]['dependency_waits'][0]['dependency'],500)

    def test_new_event_domains_instants_and_raw_sqlite_status(self):
        fixture=tests_cli.CLITests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        fixture.arm()
        instant={80,81,82,83,84,85,89,91,92}
        with fixture.path.open('r+b') as handle:
            for index,t in enumerate(range(80,95)):
                flags=261 if t in (88,93,94) else 2 if t in (80,81) else 0
                start=100+index*10;end=start if t in instant else start+5
                os.pwrite(handle.fileno(),tests_cli.EVENT.pack(start,end,300,t,2,flags,1,301,0,index+1),4096+index*64)
        fixture.patch(24,15);fixture.stop()
        result=fixture.call('decode',fixture.path,1,fixture.identity);self.assertEqual(result.returncode,0,result.stderr)
        decoded=fixture.rows(result);self.assertEqual(len(decoded),16)
        for row in decoded[:-1]:
            self.assertEqual(row['clock_domain'],'cpu_steady_ns')
            self.assertEqual(row['duration_ns'],None if row['type'] in instant else 5)
            if row['type'] in (88,93,94):self.assertEqual(row['flags'],261)
        a=analyze(decoded);self.assertFalse(a['detailed_probes']['capture_usable']);self.assertFalse(a['cpu_present_streams'])

if __name__=='__main__':unittest.main()
