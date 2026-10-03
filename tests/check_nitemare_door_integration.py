"""Guard the fail-closed production boundary and generated translator wiring."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
loader = (root / 'src/resourcefiles/file_nitemare.cpp').read_text()
assert 'Nitemare_DoorUse' in loader
assert 'Door_Open' not in loader
assert 'arg1 = 16' not in loader and 'arg2 = 300' not in loader
assert 'id, doorAxis,' in loader and 'arg0 = %d' in loader
specials = (root / 'src/lnspecials.h').read_text()
assert 'DEFINE_SPECIAL(Nitemare_DoorUse, 14, 1)' in specials
runtime = (root / 'src/lnspec.cpp').read_text()
adapter = runtime.split('FUNC(Nitemare_DoorUse)\n{', 1)[1].split('\nclass EVDoor', 1)[0]
assert 'NitemareDoor::CanActivate(args[0])' in adapter
for forbidden in ('new EVDoor', 'slideAmount', 'LinkZones', 'SndSeqPlayer', 'return 1;'):
    assert forbidden not in adapter
print('Nitemare door integration guards passed')
