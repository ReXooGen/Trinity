# PE 2949 menu-route inventory

Source: `src/gui/menu.cpp` (45 `Render*` entry points).

Visible controls: **208**. Controls present only inside a block comment: **2**.

| # | tab | route id | entry point | widget | label | State binding | menu.cpp |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | PLAYER | `combat_options` | `RenderCombatOptions` | Toggle | One-Hit Kill | `st.oneHitKill` | 88 |
| 2 | PLAYER | `combat_options` | `RenderCombatOptions` | Toggle | God Mode | `st.godMode` | 90 |
| 3 | PLAYER | `combat_options` | `RenderCombatOptions` | Toggle | Infinite Item Durability | `st.infDurability` | 94 |
| 4 | PLAYER | `combat_options` | `RenderCombatOptions` | Toggle | No Fall Damage | `st.noFallDamage` | 96 |
| 5 | PLAYER | `combat_options` | `RenderCombatOptions` | Toggle | Infinite Stamina & Mount | `st.infStamina` | 98 |
| 6 | PLAYER | `combat_options` | `RenderCombatOptions` | Toggle | Infinite Spirit | `st.infSpirit` | 104 |
| 7 | PLAYER | `combat_options` | `RenderCombatOptions` | Toggle | No Bounty | `st.noBounty` | 106 |
| 8 | PLAYER | `combat_options` | `RenderCombatOptions` | FloatOption | Outgoing Damage | `st.dmgOutMult` | 112 |
| 9 | PLAYER | `combat_options` | `RenderCombatOptions` | FloatOption | Incoming Damage | `st.dmgInMult` | 114 |
| 10 | PLAYER | `<TabPlayer root>` | `RenderPlayer` | Submenu | Combat & Gameplay Options | - | 127 |
| 11 | PLAYER | `<TabPlayer root>` | `RenderPlayer` | Submenu | Edit Equipment | - | 130 |
| 12 | PLAYER | `<TabPlayer root>` | `RenderPlayer` | Toggle | Max Worker Level & Skills | `st.workerMaxLevelAndSkills` | 137 |
| 13 | PLAYER | `<TabPlayer root>` | `RenderPlayer` | ToggleFloat | Super Run | `st.superRun`, `st.superRunMult` | 153 |
| 14 | PLAYER | `<TabPlayer root>` | `RenderPlayer` | ToggleFloat | Super Jump | `st.superJump`, `st.superJumpMult` | 155 |
| 15 | PLAYER | `<TabPlayer root>` | `RenderPlayer` | ToggleFloat | Free Flight | `st.flightSpeed`, `st.freeFlight` | 157 |
| 16 | PLAYER | `<TabPlayer root>` | `RenderPlayer` | ToggleFloat | Trust Multiplier | `st.trustMult`, `st.trustMultVal` | 207 |
| 17 | ? | `?` | `RenderDyeSlots` | Combo | Character | - | 339 |
| 18 | ? | `?` | `RenderDyeSlots` | Option | Character not loaded | - | 349 |
| 19 | ? | `?` | `RenderDyeSlots` | Option | Waiting for your equipment... | - | 352 |
| 20 | ? | `?` | `RenderDyeSlots` | Option | Inject All Dyes to Save Data | - | 358 |
| 21 | ? | `?` | `RenderDyeSlots` | SubmenuItem |  | - | 417 |
| 22 | ? | `?` | `RenderDyeSlots` | Option | Nothing equipped | - | 434 |
| 23 | ? | `?` | `RenderDyeSlots` | Option | Nothing dyeable equipped | - | 436 |
| 24 | ? | `?` | `RenderDyeSlots` | Option |  | - | 445 |
| 25 | ? | `?` | `RenderDyeEdit` | Combo | Dye Zone | - | 484 |
| 26 | ? | `?` | `RenderDyeEdit` | Combo | Color Family | - | 486 |
| 27 | ? | `?` | `RenderDyeEdit` | SwatchRow | , rgb, cnt, &s_dyeCursor[row], mark,
                neutral ?  | - | 519 |
| 28 | ? | `?` | `RenderDyeEdit` | Submenu | Custom Color | - | 538 |
| 29 | ? | `?` | `RenderDyeEdit` | Option | Remove All Dye (Reset) | - | 540 |
| 30 | ? | `?` | `RenderDyeEdit` | IntOption | Material | - | 547 |
| 31 | ? | `?` | `RenderDyeEdit` | IntOption | Condition % | - | 549 |
| 32 | ? | `?` | `RenderDyeCustom` | IntOption | Red | - | 576 |
| 33 | ? | `?` | `RenderDyeCustom` | IntOption | Green | - | 577 |
| 34 | ? | `?` | `RenderDyeCustom` | IntOption | Blue | - | 578 |
| 35 | ? | `?` | `RenderDyeCustom` | SwatchRow | Apply This Color | - | 586 |
| 36 | ? | `?` | `RenderDyeCustom` | Option | Load Current | - | 590 |
| 37 | PLAYER | `equipslots` | `RenderEquipSlots` | Combo | Character | - | 641 |
| 38 | PLAYER | `equipslots` | `RenderEquipSlots` | Option | Character not loaded | - | 649 |
| 39 | PLAYER | `equipslots` | `RenderEquipSlots` | Option | Waiting for your equipment... | - | 652 |
| 40 | PLAYER | `equipslots` | `RenderEquipSlots` | Option | Repair All Gear | - | 657 |
| 41 | PLAYER | `equipslots` | `RenderEquipSlots` | Option | Max Refine All (+10) | - | 666 |
| 42 | PLAYER | `equipslots` | `RenderEquipSlots` | Option | Unlock All Sockets | - | 675 |
| 43 | PLAYER | `equipslots` | `RenderEquipSlots` | SubmenuEquipItem | Refine this piece and edit its abyss-gear sockets. | - | 706 |
| 44 | PLAYER | `equipslots` | `RenderEquipSlots` | Option | Nothing equipped | - | 718 |
| 45 | PLAYER | `equipedit` | `RenderEquipEdit` | Option | Not equipped | - | 730 |
| 46 | PLAYER | `equipedit` | `RenderEquipEdit` | Option | Unlock all sockets | - | 741 |
| 47 | PLAYER | `equipedit` | `RenderEquipEdit` | Option | Clear all sockets | - | 751 |
| 48 | PLAYER | `equipedit` | `RenderEquipEdit` | IntOption | Refinement | - | 766 |
| 49 | PLAYER | `equipedit` | `RenderEquipEdit` | SubmenuItem | Change Equipment (Bypass Story / Quest Lock) | - | 779 |
| 50 | PLAYER | `equipedit` | `RenderEquipEdit` | Option | No sockets | - | 788 |
| 51 | PLAYER | `equipedit` | `RenderEquipEdit` | Option | Note: not saving yet | - | 794 |
| 52 | PLAYER | `equipedit` | `RenderEquipEdit` | Option | This socket is locked. Click to unlock all sockets on this piece. | - | 804 |
| 53 | PLAYER | `equipedit` | `RenderEquipEdit` | SubmenuItem | Change or remove this abyss gear. | - | 814 |
| 54 | PLAYER | `equipswap` | `RenderEquipSwap` | Combo | Character Filter | - | 844 |
| 55 | PLAYER | `equipswap` | `RenderEquipSwap` | Combo | Category Filter | - | 856 |
| 56 | PLAYER | `equipswap` | `RenderEquipSwap` | Search | Find any weapon, shield, or armor to equip. | - | 859 |
| 57 | PLAYER | `equipswap` | `RenderEquipSwap` | OptionItem |  | - | 936 |
| 58 | PLAYER | `equipswap` | `RenderEquipSwap` | Option | No matches | - | 955 |
| 59 | PLAYER | `equipswap` | `RenderEquipSwap` | Option | More matches... | - | 957 |
| 60 | PLAYER | `equipgear` | `RenderEquipGear` | OptionItem | - Empty this socket - | - | 970 |
| 61 | PLAYER | `equipgear` | `RenderEquipGear` | Option | No abyss gears found | - | 987 |
| 62 | PLAYER | `equipgear` | `RenderEquipGear` | Search | Find an abyss gear by name. | - | 993 |
| 63 | PLAYER | `equipgear` | `RenderEquipGear` | OptionItemWithBuff | Socket this abyss gear. | - | 1006 |
| 64 | PLAYER | `equipgear` | `RenderEquipGear` | Option | No matches | - | 1021 |
| 65 | PLAYER | `equipgear` | `RenderEquipGear` | Option | More matches... | - | 1023 |
| 66 | WORLD | `world_time_presets` | `RenderTimePresets` | Option | Current in-game world time. | - | 1038 |
| 67 | WORLD | `world_time_presets` | `RenderTimePresets` | Option | Dawn / Morning (06:00) | - | 1041 |
| 68 | WORLD | `world_time_presets` | `RenderTimePresets` | Option | Midday / Noon (12:00) | - | 1047 |
| 69 | WORLD | `world_time_presets` | `RenderTimePresets` | Option | Sunset / Golden Hour (18:00) | - | 1053 |
| 70 | WORLD | `world_time_presets` | `RenderTimePresets` | Option | Midnight / Night (00:00) | - | 1059 |
| 71 | WORLD | `world_time_presets` | `RenderTimePresets` | IntAction | Set Exact Hour | - | 1066 |
| 72 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | Combo | Weather Preset | - | 1092 |
| 73 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | Toggle | Clear Distant Fog | `st.clearDistantFog` | 1100 |
| 74 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | Toggle | Force Clear Sky | `st.forceClearSky` | 1101 |
| 75 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | Option | Instant Clear Weather | - | 1103 |
| 76 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Rain Intensity | `st.rainIntensity` | 1115 |
| 77 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Snow Intensity | `st.snowIntensity` | 1116 |
| 78 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Dust / Sandstorm | `st.dustIntensity` | 1117 |
| 79 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Cloud Thickness | `st.cloudThick` | 1120 |
| 80 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Cloud Top Altitude | `st.cloudTop` | 1121 |
| 81 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Cloud Base Altitude | `st.cloudBase` | 1122 |
| 82 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Cloud Drift Speed | `st.cloudScrollSpeed` | 1123 |
| 83 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Fog Scattering (A) | `st.fogA` | 1126 |
| 84 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Fog Horizon Blend (B) | `st.fogB` | 1127 |
| 85 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Wind Speed Multiplier | `st.windMultiplier` | 1130 |
| 86 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Wind Gust Strength | `st.windGust` | 1131 |
| 87 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | FloatOption | Turbulence Lift | `st.windTurbLift` | 1132 |
| 88 | WORLD | `world_weather` | `RenderWeatherAtmosphere` | Toggle | No Wind | `st.noWind` | 1133 |
| 89 | WORLD | `<TabWorld root>` | `RenderWorld` | ToggleFloat | Game Speed | `st.gameSpeed`, `st.gameSpeedMult` | 1148 |
| 90 | WORLD | `<TabWorld root>` | `RenderWorld` | Toggle | Freeze Time of Day | `st.timeFrozen` | 1154 |
| 91 | WORLD | `<TabWorld root>` | `RenderWorld` | IntAction | Advance Time (+) | - | 1161 |
| 92 | WORLD | `<TabWorld root>` | `RenderWorld` | IntAction | Rewind Time (-) | - | 1172 |
| 93 | WORLD | `<TabWorld root>` | `RenderWorld` | Submenu | Time of Day Presets | - | 1181 |
| 94 | WORLD | `<TabWorld root>` | `RenderWorld` | Submenu | Weather & Atmosphere | - | 1182 |
| 95 | TRAVEL | `<TabTravel root>` | `RenderTravel` | Option | Copy these coordinates to the clipboard. | - | 1200 |
| 96 | TRAVEL | `<TabTravel root>` | `RenderTravel` | Option | No position yet | - | 1208 |
| 97 | TRAVEL | `<TabTravel root>` | `RenderTravel` | Option |  | - | 1233 |
| 98 | TRAVEL | `<TabTravel root>` | `RenderTravel` | FloatOption | Sky Arrival Altitude | `st.markerFallbackHeight` | 1261 |
| 99 | TRAVEL | `<TabTravel root>` | `RenderTravel` | Submenu | Saved Locations | - | 1269 |
| 100 | TRAVEL | `<TabTravel root>` | `RenderTravel` | Submenu | Fast Travel | - | 1273 |
| 101 | ? | `?` | `RenderFilteredList` | Search |  | - | 1297 |
| 102 | ? | `?` | `RenderFilteredList` | Option |  | - | 1304 |
| 103 | TRAVEL | `?` | `RenderCategoryList` | SubmenuItem |  | - | 1325 |
| 104 | TRAVEL | `ftcats` | `RenderFastTravelCats` | Option | Building destination list... | - | 1345 |
| 105 | ? | `ftnodes` | `RenderFastTravelNodes` | Option | No locations | - | 1377 |
| 106 | ? | `ftnodes` | `RenderFastTravelNodes` | Option |  | - | 1399 |
| 107 | TRAVEL | `saved_locs` | `RenderSavedLocations` | Option | + Save Current Location | - | 1445 |
| 108 | TRAVEL | `saved_locs` | `RenderSavedLocations` | Option | No saved locations | - | 1469 |
| 109 | TRAVEL | `saved_locs` | `RenderSavedLocations` | Option | Clear All Saved Locations | - | 1502 |
| 110 | ? | `loc_manage` | `RenderSavedLocationManage` | Option | Location not found | - | 1519 |
| 111 | ? | `loc_manage` | `RenderSavedLocationManage` | Option | Instantly warp your player to these coordinates. | - | 1531 |
| 112 | ? | `loc_manage` | `RenderSavedLocationManage` | TextInput | Name | - | 1539 |
| 113 | ? | `loc_manage` | `RenderSavedLocationManage` | Option | Update to Current Position | - | 1546 |
| 114 | ? | `loc_manage` | `RenderSavedLocationManage` | Option | Delete This Location | - | 1562 |
| 115 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | Submenu | Money & Currency | - | 1722 |
| 116 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | Submenu | Abyss Items & Artifacts | - | 1723 |
| 117 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | Submenu | Add Item | - | 1724 |
| 118 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | Submenu | Restore Items | - | 1725 |
| 119 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | Submenu | Item Editor | - | 1726 |
| 120 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | Option | Slot Size (unsupported) | - | 1735 |
| 121 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | ToggleInt | Slot Size | `st.invSlotSize`, `st.invSlotSizeVal` | 1738 |
| 122 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | Toggle | Max Stack Size | `st.invStackSize` | 1742 |
| 123 | INVENTORY | `<TabInventory root>` | `RenderInventoryHome` | IntOption | Set Max Stack Value | `st.invStackSizeVal` | 1745 |
| 124 | INVENTORY | `invedit` | `RenderInventoryEditor` | Option | Loading inventory... | - | 1762 |
| 125 | INVENTORY | `invedit` | `RenderInventoryEditor` | Option | Refresh | - | 1792 |
| 126 | ? | `invstore` | `RenderInventoryStorage` | Option | Empty | - | 1814 |
| 127 | ? | `invstore` | `RenderInventoryStorage` | Search | Find an item anywhere in this storage, whatever category it is in. | - | 1825 |
| 128 | ? | `invstore` | `RenderInventoryStorage` | Option | No matches | - | 1856 |
| 129 | ? | `?` | `RenderSetAll` | IntAction | Set All | - | 1875 |
| 130 | ? | `invcat` | `RenderInventoryCat` | Option | Empty | - | 1897 |
| 131 | ? | `invcat` | `RenderInventoryCat` | Search | Narrow this category down by name. | - | 1902 |
| 132 | ? | `invcat` | `RenderInventoryCat` | Option | No matches | - | 1916 |
| 133 | INVENTORY | `invadd` | `RenderInventoryAdd` | Option | Catalog unavailable | - | 1989 |
| 134 | INVENTORY | `invadd` | `RenderInventoryAdd` | Search | Find any item in the game by name, whatever category it is in. | - | 1998 |
| 135 | INVENTORY | `invadd` | `RenderInventoryAdd` | Option | No matches | - | 2029 |
| 136 | INVENTORY | `invadd` | `RenderInventoryAdd` | Option | More matches... | - | 2031 |
| 137 | ? | `?` | `RenderAddAll` | IntAction | Add All | - | 2049 |
| 138 | ? | `invaddcat` | `RenderInventoryAddCat` | Option | Empty | - | 2080 |
| 139 | ? | `invaddcat` | `RenderInventoryAddCat` | Search | Narrow this category down by name. | - | 2085 |
| 140 | ? | `invaddcat` | `RenderInventoryAddCat` | Option | No matches | - | 2103 |
| 141 | INVENTORY | `invmoney_opt` | `RenderInventoryMoneyOptional` | Option | >> Clear Bugged/Fake Wallet Coins << | - | 2116 |
| 142 | INVENTORY | `invmoney_opt` | `RenderInventoryMoneyOptional` | Option | Consolidate All Money Stacks | - | 2123 |
| 143 | INVENTORY | `invmoney` | `RenderInventoryMoney` | IntOption | Direct Silver Amount | - | 2139 |
| 144 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Option | >> Set Wallet to Exact Amount << | - | 2142 |
| 145 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Option | >> Add Amount to Existing Wallet << | - | 2155 |
| 146 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Option | Set to 1,000,000 Silver (1 Million) | - | 2165 |
| 147 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Option | Set to 10,000,000 Silver (10 Million) | - | 2171 |
| 148 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Option | Set to 20,000,000 Silver (20 Million) | - | 2177 |
| 149 | INVENTORY | `invmoney` | `RenderInventoryMoney` | IntOption | Full Silver Pouches (Count) | - | 2184 |
| 150 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Option | >> Spawn Full Silver Pouches << | - | 2187 |
| 151 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Option | >> Cash In All Pouches (Instant Liquidate) << | - | 2196 |
| 152 | INVENTORY | `invmoney` | `RenderInventoryMoney` | Submenu | Optional | - | 2207 |
| 153 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Scans your inventory for unique Sealed Abyss Artifact IDs (0001 to 0150). | - | 2229 |
| 154 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | IntOption | Target Total Sealed Artifacts | - | 2231 |
| 155 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | >> Add Missing Sealed Artifacts to Target << | - | 2241 |
| 156 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | >> Add ALL Missing Sealed Artifacts (1 to 150) << | - | 2250 |
| 157 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Clean Duplicate Sealed Artifacts | - | 2260 |
| 158 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | IntOption | Abyss Artifacts (Count) | - | 2271 |
| 159 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | >> Spawn Abyss Artifacts (Usable Material) << | - | 2274 |
| 160 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 100x Abyss Artifacts | - | 2284 |
| 161 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 500x Abyss Artifacts | - | 2290 |
| 162 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 50x Blessing of the Immortal (Skill Points) | - | 2296 |
| 163 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 50x Advanced Skill Manuals | - | 2302 |
| 164 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 50x Abyssal Seeds | - | 2308 |
| 165 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 50x Abyss Cells | - | 2314 |
| 166 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 10x Vitality of the Abyss (+30 HP) | - | 2320 |
| 167 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 10x Spirit of the Abyss (+2 MP) | - | 2326 |
| 168 | INVENTORY | `invabyss` | `RenderInventoryAbyss` | Option | Add 10x Breath of the Abyss (+3 SP) | - | 2332 |
| 169 | INVENTORY | `?` | `RenderRestoreCategoryPage` | Option | Current ownership status across your inventory, bags, and storages. | - | 2465 |
| 170 | INVENTORY | `?` | `RenderRestoreCategoryPage` | Option | >> Restore All Missing in Category << | - | 2472 |
| 171 | INVENTORY | `?` | `RenderRestoreCategoryPage` | Search | Search items in this category by name or internal key. | - | 2488 |
| 172 | INVENTORY | `?` | `RenderRestoreCategoryPage` | OptionItemWithSubtitle |  | - | 2536 |
| 173 | INVENTORY | `?` | `RenderRestoreCategoryPage` | Option | No matches | - | 2547 |
| 174 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 1. Bounty Notices (Wanted Posters) | - | 2598 |
| 175 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 2. Documents & Lore Books | - | 2600 |
| 176 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 3. Recipes & Crafting Manuals | - | 2602 |
| 177 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 4. Quest Keys, Passes & Memories | - | 2604 |
| 178 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 5. Collectibles & Collector's Chest | - | 2606 |
| 179 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 6. Unique Quest Weapons & Boss Gear | - | 2608 |
| 180 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 7. Mount, Mecha & Vehicle Gear | - | 2610 |
| 181 | INVENTORY | `invrestore_catalog` | `RenderRestoreCatalogArchive` | Submenu | 8. Rare Medals, Tokens & Artifacts | - | 2612 |
| 182 | INVENTORY | `invrestore` | `RenderInventoryRestore` | Option | No Lost / Sold Items Recorded | - | 2628 |
| 183 | INVENTORY | `invrestore` | `RenderInventoryRestore` | Option | Items you sold, discarded, or deleted. Select any item to buyback and restore. | - | 2636 |
| 184 | INVENTORY | `invrestore` | `RenderInventoryRestore` | Option | >> Restore All Lost & Sold Items << | - | 2638 |
| 185 | INVENTORY | `invrestore` | `RenderInventoryRestore` | Option | Clear Lost & Sold History | - | 2645 |
| 186 | INVENTORY | `invrestore` | `RenderInventoryRestore` | Search | Filter lost items by name or key... | - | 2652 |
| 187 | INVENTORY | `invrestore` | `RenderInventoryRestore` | OptionItemWithSubtitle |  | - | 2703 |
| 188 | INVENTORY | `invrestore` | `RenderInventoryRestore` | Option | No matches | - | 2715 |
| 189 | INVENTORY | `invrestore` | `RenderInventoryRestore` | Submenu | Quest & Special Item Catalog Archive | - | 2718 |
| 190 | SYSTEM | `keybinds` | `RenderKeybinds` | Option | Reset All Keybinds | - | 2996 |
| 191 | SYSTEM | `font_settings` | `RenderFontSettings` | Combo | Built-In Fallback | `st.builtInFontIndex` | 3049 |
| 192 | SYSTEM | `font_settings` | `RenderFontSettings` | Toggle | Enable Custom Font | `st.useCustomFont` | 3055 |
| 193 | SYSTEM | `font_settings` | `RenderFontSettings` | OptionItem | No Fonts Found | - | 3066 |
| 194 | SYSTEM | `font_settings` | `RenderFontSettings` | Combo | Select Custom Font | - | 3081 |
| 195 | SYSTEM | `menu_ui` | `RenderMenuUISettings` | FloatOption | Menu Scale | `st.menuScale` | 3108 |
| 196 | SYSTEM | `menu_ui` | `RenderMenuUISettings` | Toggle | Show Item Tooltip | `st.showItemTooltip` | 3126 |
| 197 | SYSTEM | `menu_ui` | `RenderMenuUISettings` | FloatOption | Tooltip Image Size | `st.tooltipImageScale` | 3129 |
| 198 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Submenu | Keybinds | - | 3151 |
| 199 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Submenu | Menu UI Settings | - | 3154 |
| 200 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Combo | Theme Color | `st.themeIndex` | 3161 |
| 201 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Toggle | PlayStation Icons | `st.playstationIcons` | 3167 |
| 202 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Combo | Language | - | 3179 |
| 203 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Submenu | Title Font | - | 3189 |
| 204 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Toggle | Show FPS Counter | `st.showFps` | 3195 |
| 205 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Toggle | Show Console Window | `st.showConsole` | 3197 |
| 206 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Option | Game version automatically detected by Trinity engine. | - | 3208 |
| 207 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Toggle | Auto Save Features | `st.autoSave` | 3210 |
| 208 | SYSTEM | `<TabSystem root>` | `RenderSystem` | Option | Reset All to Default | - | 3213 |

## Controls compiled out of the menu

| label | widget | menu.cpp | note |
| --- | --- | --- | --- |
| No Clip | ToggleFloat | 167 | inside a block comment - not reachable by the player |
| Escape: Back To Last Safe Spot | Option | 186 | inside a block comment - not reachable by the player |
