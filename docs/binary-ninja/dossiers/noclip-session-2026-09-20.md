# No Clip PE 2944 — діагностика та поточний стан

Дата: 2026-09-20  
Гра: Crimson Desert, PE 2944  
Статус: не завершено; справжнє вимкнення collision ще не реалізоване.

## Висновок

Поточний No Clip працює через position pin: Trinity кожен movement tick обчислює нову позицію та записує її у proxy. Це іноді дозволяє пройти крізь geometry, але не вимикає physics collision. Якщо character controller або animation path виконує depenetration, ground correction, climb/fall correction чи інший solver step, персонаж може опинитися всередині текстури та застрягти.

Space/Ctrl не є справжнім рішенням. Вони іноді переводять персонажа у native airborne/falling path, де collision correction поводиться інакше, але користувач підтвердив, що застрягання залишається і з Space/Ctrl, і без них, а спроба вибратися через Space/Ctrl також не допомагає.

Фінальна ціль: per-body collision disable для local-player proxy із гарантованим restore, незалежно від animation state, grounded/falling/climbing state або типу geometry.

## Підтверджені PE 2944 Havok strings/types

| Об’єкт | VA |
|---|---:|
| `hknpDisableCollisionFilter` | `0x145308A90` |
| `hknpSetBodyCollisionFilterInfoCommand` | `0x1453015B8` |
| `collisionFilterInfo` | `0x145302AA0` |
| `hknpRebuildBodyCollisionCachesCommand` | `0x145301CC0` |
| `hknpUpdateBodyCollisionCachesCommand` | `0x145301DF8` |
| `hknpSetWorldCollisionFilterCommand` | `0x1453049C0` |
| `hknpGroupCollisionFilter` | `0x14530A050` |
| `hknpPairCollisionFilter` | `0x1453083D0` |
| `hknpConstraintCollisionFilter` | `0x145308718` |

Command/debug strings:

- `setBodyCollisionFilterInfo Id=` — `0x145311EC8`;
- `filterInfo=` — `0x145311EE8`;
- `disable` — `0x145311EF8`;
- `rebuildBodyCollisionCaches Id=` — `0x145311F28`;
- `updateBodyCollisionCaches Id=` — `0x145311F48`.

Binary Ninja бачить типізовані RTTI-типи `hknpDisableCollisionFilter::VTable` та `hknpCollisionFilter::hknpDisableCollisionFilter::VTable`. Також присутній MSVC type name `. ?AVhknpDisableCollisionFilter@@` без пробілу після крапки у фактичному рядку: `.?AVhknpDisableCollisionFilter@@`. Constructor/factory та live vtable address ще не ідентифіковані.

## Movement/body runtime evidence

- original movement integrator: `CrimsonDesert.exe+0x4282080`, VA `0x144282080`;
- local movement owner спостерігався як `RCX`, зокрема `0x3A36B800500` та пізніше `0x3F5343B0500`;
- історично `moveOwner + 0x48` приводив до physics manager;
- історично `moveOwner + 0x38` містив body handle/ідентифікатор;
- у попередньому runtime-контексті manager vtable `+0x80` приводив до `getBody`;
- body pointer, manager і vtable нестабільні після streaming, respawn або controller rebuild.

Пізніший live capture показав, що актуальні manager/vtable вже відрізнялися від ранніх адрес. Тому жоден body/manager/vtable не можна кешувати між ticks.

## Property API

У попередньому manager vtable:

- `vtable + 0xF0` → `getBodyPropertyImpl`, target `0x14324EE20`;
- `vtable + 0xF8` → `getActiveBodies`;
- `vtable + 0x100` → `getMotion`;
- `vtable + 0x108` → `getConstraints`.

У `sub_144283700` видно read property call приблизно такого вигляду:

```text
getBodyPropertyImpl(manager, bodyId, 0xF001, 8, out)
```

`vtable + 0xF0` — accessor/read path, не setter. Його не можна використовувати як collision disable.

## Body memory experiments

Для раніше знайденого local body приблизно `0x3A383D06380`:

- `body + 0x54` часто читався як filter-related data;
- `body + 0x80` читався та записувався physics state;
- `body + 0xB0` виглядав як pointer/metadata field.

Тимчасовий write у `body + 0x54` був нормалізований/перезаписаний рушієм. Це не простий bool/flag. Blind write у body structure заборонений.

Execution probe на `0x1432570F2` показував різні filter values, зокрема `0x01002086` та `0x19002086`. Це generic decoder для багатьох bodies, не безпечний глобальний patch point.

Read-only hardware probes на command strings не дали hits під час звичайного руху. Усі probes видалені; активних CE breakpoint/watchpoint немає.

## Поточний код Trinity

Основні файли:

- `src/game/noclip_logic.h` — step calculation;
- `src/game/teleport.cpp` — hooks, input, position pin і No Clip lifecycle;
- `src/game/teleport.h` — public No Clip state;
- `tests/noclip_logic_tests.cpp` — unit tests.

Поточний рух:

1. `hkLocoStep` публікує dt та heading.
2. `hkMoveUpdate` викликає `BeginNoClipTick` перед оригінальним integrator.
3. Читається позиція `moveOwner + 0x90`.
4. `ComputeNoClipStep` рахує target.
5. `PinProxyPosition` записує позицію через marker-teleport write path.
6. Оригінальний integrator виконується.
7. `FinishNoClipTick` вимірює drift і за потреби повторює pin.

Цей шлях не вимикає collision. Він лише намагається перезаписати наслідок physics correction.

Доданий `ResolveLocalPhysicsBody(owner)`, який runtime-резолвить manager/body handle/vtable/getBody, не кешує body, перевіряє vtable/getBody у межах main game image і захищений від exception. Він поки не підключений до реального filter setter.

## Зміни, внесені під час сесії

1. Dynamic local physics body resolver.
2. Module-range validation для vtable/getBody.
3. No Clip підключений до native airborne branch.
4. Для No Clip вимкнений automatic ground-contact reset, який потрібен Free Flight.
5. Для No Clip airborne state примусово активується одразу після enable.
6. Release build проходив успішно після змін.
7. `build/Release/Trinity.asi` автоматично розгортався build script у Steam directories.

Ці зміни покращують workaround, але не є true collision disable.

## Поточний live-симптом

Користувач підтвердив:

- No Clip все ще працює нестабільно;
- персонаж застрягає всередині текстур;
- застрягання відбувається незалежно від Space/Ctrl;
- Space/Ctrl не гарантують вихід із geometry;
- найкраще проходження раніше спостерігалося під час fall animation, але також нестабільно;
- screenshot показував персонажа біля/в geometry, де position write більше не гарантує вихід.

## Відкинуті гіпотези

### Position pin достатній

Ні. Integrator, animation, controller state або streaming можуть скоригувати стан після pin. Read-back одного tick не гарантує наступного tick.

### Space/Ctrl вимикають collision

Ні. Вони лише змінюють locomotion/airborne/fall path і не вимикають per-body collision.

### `body + 0x54` — простий collision bool

Ні. Live write experiment показав нормалізацію/перезапис.

### `manager vtable + 0xF0` — setter

Ні. Це `getBodyPropertyImpl`, read/property accessor.

### Body pointer стабільний

Ні. Body/manager/vtable змінюються при streaming, respawn і rebuild controller.

## Що ще потрібно знайти

1. Runtime identity актуального local-player body, body ID та body-local filter state.
2. Реальний setter або command-stream enqueue path для `setBodyCollisionFilterInfo`.
3. `rebuildBodyCollisionCaches`/`updateBodyCollisionCaches` invocation path.
4. Constructor/factory registration для `hknpDisableCollisionFilter`.
5. Точний формат `filterInfo` та `disable` параметра.
6. Безпечний restore path для No Clip off, body replacement, exception/failure та scene transition.
7. Доказ, що змінюється тільки local-player body, а не shared filter object.

## Рекомендований план продовження

### Phase 1 — read-only runtime capture

- На game thread отримувати актуальний body кожен tick.
- Викликати лише read-only `getBodyPropertyImpl`.
- Логувати body pointer, body handle, manager, vtable та property до/після body replacement.

### Phase 2 — command/factory discovery

- Знайти Havok command stream context.
- Знайти constructor/factory registration disable filter.
- Визначити ABI та physics-thread/lock requirements.

### Phase 3 — reversible live experiment

- Зберегти original filter info.
- Застосувати disable лише до local body.
- Rebuild/update caches.
- Перевірити wall/floor/ceiling у walk, fall, climb та idle.
- Restore original filter і перевірити повернення collision.

### Phase 4 — production integration

- Створити `NoClipCollisionState` з body identity, original filter, disabled flag і restore status.
- Detect body replacement та повторювати lifecycle.
- Fail closed, якщо setter/factory не resolved.
- Position pin залишити лише fallback або прибрати після live proof native filter path.

## Acceptance criteria

- collision вимикається для local-player proxy незалежно від animation state;
- wall/floor/ceiling проходяться стабільно;
- Space/Ctrl не потрібні;
- player не застрягає всередині geometry;
- No Clip off відновлює original filter;
- streaming/body replacement не залишає collision вимкненим на неправильному body;
- інші bodies не втрачають collision;
- live test підтверджений після свіжого launch;
- build/package evidence відокремлені від live semantic proof.
