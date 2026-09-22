import json, sys
sys.path.insert(0, ".")
from ce import CE

script = "[ENABLE]\nalloc(x,4)\n[DISABLE]\ndealloc(x)\n"
ce = CE()
try:
    r = ce.call("auto_assemble_check", {"script": script})
    print(json.dumps(r, indent=2))
finally:
    ce.close()
