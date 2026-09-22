# AOB literals outside the registry headers

Scanned against `E:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe` (image base 0x140000000).

| file:line | pattern | matches | RVA | section | decoder |
| --- | --- | --- | --- | --- | --- |
| `src/game/equipment.cpp:1574` | `48 89 74 24 10 57 48 83 EC 20 48 83 79 60 00` | 1 | 0x488C47D | .sbss | mov qword ptr [rsp + 0x10], rsi ; push rdi ; sub rsp, 0x20 |
| `src/game/inventory.cpp:2184` | `48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 ` | 8 | 0x3C4780 | .sbss | mov qword ptr [rsp + 0x10], rbx ; mov qword ptr [rsp + 8], rcx ; push rbp |
| `src/game/inventory.cpp:2185` | `48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 ` | 8 | 0x3C4780 | .sbss | mov qword ptr [rsp + 0x10], rbx ; mov qword ptr [rsp + 8], rcx ; push rbp |
| `src/game/inventory.cpp:2186` | `48 89 5C 24 ? 48 89 4C 24 ? 55 56 57 41 54 41 55 41 ` | 8 | 0x4AE580 | .sbss | mov qword ptr [rsp + 0x18], rbx ; mov qword ptr [rsp + 8], rcx ; push rbp |
| `src/game/inventory.cpp:2187` | `48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 ` | 1 | 0x3FDE910 | .sbss | mov qword ptr [rsp + 8], rbx ; mov qword ptr [rsp + 0x10], rsi ; push rdi |
| `src/game/inventory.cpp:2188` | `48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83` | 8 | 0x393F50 | .sbss | mov qword ptr [rsp + 0x10], rbx ; push rbp ; push rsi |
| `src/game/teleport.cpp:530` | `48 8B 05 ?? ?? ?? ?? 48 8B 98 A8 00 00 00 C4 C1 78 1` | 1 | 0xDEFD0E | .sbss | mov rax, qword ptr [rip + 0x5f794f3] ; mov rbx, qword ptr [rax + 0xa8] ; vmovups xmm0, xmmword ptr [r12] |
| `src/mem/scanner.h:25` | `48 8B 05 ?? ?? ?? ?? 48 85 C0` | 8 | 0x73F25 | .sbss | mov rax, qword ptr [rip + 0x6b990bc] ; test rax, rax ; je 0x140073f4b |
