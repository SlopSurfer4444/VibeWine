"""Retained writer-interleaving driver bound to today's maintained protocol."""
import pathlib,re,subprocess,tempfile,unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]

class SharedAdmittedWriterTests(unittest.TestCase):
    def test_existing_writer_close_overflow_and_generation_barriers(self):
        with tempfile.TemporaryDirectory(prefix='detail-protocol-') as td:
            work=pathlib.Path(td);historical=ROOT/'renderer/wave6/consumer'
            # The maintained controller now requires explicit capacity planning.
            # Adapt only old driver call sites, keeping production code exact.
            driver=re.sub(r'arm\((\d+|UINT32_MAX)\)',r'arm(\1,1024,128)',(historical/'tests_protocol.cpp').read_text())
            (work/'tests_protocol.cpp').write_text(driver)
            (work/'legacy').mkdir();(work/'legacy/trace_core.hpp').write_bytes((historical/'legacy/trace_core.hpp').read_bytes())
            core=(ROOT/'tools/telemetry/trace_core.hpp').read_text().replace('../../components/dxmt/src/d3d11/cs2_trace_protocol.hpp',str(ROOT/'components/dxmt/src/d3d11/cs2_trace_protocol.hpp'))
            (work/'trace_core.hpp').write_text(core)
            result=subprocess.run(['xcrun','clang++','-std=c++20','-O1','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined',str(work/'tests_protocol.cpp'),'-o',str(work/'protocol')],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(work/'protocol')],capture_output=True,text=True,timeout=45)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('failed=0 live_wine=false',result.stdout);self.assertEqual(result.stderr,'')

if __name__=='__main__':unittest.main()
