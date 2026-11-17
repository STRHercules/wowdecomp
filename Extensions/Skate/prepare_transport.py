"""Create the runtime adapter from the byte-preserved donor transport."""
from pathlib import Path
p = Path(__file__).parent
s = (p / "src/SkateLogic/skate-pipe.h").read_text()
s = s.replace("namespace skatewire {", "namespace skatetransport {")
s = s.replace("bool appendLog=false)", "bool appendLog=false,const wchar_t* workingDirectory=nullptr)")
s = s.replace('L"D:\\\\world of skatecraft 3.3.5\\\\native-probe",', 'workingDirectory,')
# Op 48 builds a session from client collision and op 7 loads the board preview: both need the init deadline.
s = s.replace("(op==1 || op==16 || op==30)", "(op==1 || op==16 || op==30 || op==48 || op==7)")
assert "world of skatecraft" not in s and "op==48" in s
(p / "src/SkateTransport.h").write_text("// Adapted from wotlkskate afa45f0 skate-pipe.h: configurable child directory.\n" + s)
