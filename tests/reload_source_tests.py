"""Source-level guards for lifecycle/side-effect invariants; not an in-game test."""
from pathlib import Path
import re
import unittest

SRC = Path(__file__).resolve().parents[1] / "src" / "game"


def body(source, signature):
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class ReloadSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inventory = (SRC / "inventory.cpp").read_text(encoding="utf-8")
        cls.player = (SRC / "player.cpp").read_text(encoding="utf-8")

    def test_accounting_does_not_write_item_attributes(self):
        repair = body(self.inventory, "void RepairUsedSlots(")
        writes = re.findall(r"Write\w+\([^;]+", repair)
        self.assertEqual(writes, ["Write16(bucket + kOff_InvBucket_UsedSlots, occ)"])
        self.assertIn("complete && occ < used", repair)
        self.assertNotIn("EnsureTablesResolved", repair)
        refresh = body(self.inventory, "static void RefreshImplCore(")
        self.assertNotIn("RepairUsedSlots", refresh)

    def test_identity_never_uses_its_own_published_cache(self):
        for signature in ("static int IdentifyCharacterIdentity(",
                          "int Inventory::IdentifyCharacterFromEquip("):
            identity = body(self.inventory, signature)
            self.assertNotIn("Player::Get", identity)
        identity = body(self.inventory, "static int IdentifyCharacterIdentity(")
        self.assertIn("ReadNativeEquipmentTable(comp)", identity)
        self.assertIn("stride = table.stride", identity)
        self.assertNotIn("candidateStrides", identity)
        active = body(self.inventory, "int Inventory::ActivePlayerCharacterIdx(")
        self.assertIn("return -1;", active)

    def test_controlled_body_is_separate_from_character_indices(self):
        self.assertIn("g_activeCharacterIdx{-1}", self.player)
        self.assertIn("g_characterActors[index]", body(self.player, "uintptr_t Player::GetActor("))
        self.assertIn("g_characterOwners[index]", body(self.player, "uintptr_t Player::GetOwner("))
        self.assertIn("g_owners[0]", body(self.player, "uintptr_t Player::GetControlledOwner("))
        resolve = body(self.player, "void TickResolveSelf(")
        self.assertLess(resolve.index("IdentifyPlayerCharacter(mainPlayerOwner)"),
                        resolve.index("g_owners[0].store(mainPlayerOwner"))
        self.assertIn("g_characterActors[i].store(characterActors[i]", resolve)
        self.assertIn("g_characterOwners[i].store(characterOwners[i]", resolve)
        self.assertNotIn("stillAlive", resolve)
        clear = body(self.player, "void ClearPlayerSets(")
        self.assertIn("g_playerPossessor.store(0", clear)
        self.assertIn("g_activeCharacterIdx.store(-1", clear)

    def test_capture_admits_replacement_and_recaptures_after_commit(self):
        for signature in ("void NoteContainer(", "void NoteHolder("):
            capture = body(self.inventory, signature)
            self.assertIn("cnt == kMaxCandidates", capture)
            self.assertIn("g_cand[i].tick < g_cand[at].tick", capture)
        for signature, original, capture in (
            ("void* __fastcall hkCommit(", "oCommit(holder", "NoteHolder(holder)"),
            ("void* __fastcall hkHolderInsert(", "oHolderInsert(bucket", "NoteContainer(container)"),
        ):
            hook = body(self.inventory, signature)
            self.assertLess(hook.index(capture), hook.index(original))
            self.assertGreater(hook.rindex(capture), hook.index(original))
            self.assertIn("fetch_add(1", hook)
            self.assertIn("__finally", hook)
            self.assertIn("fetch_sub(1", hook)

    def test_both_editors_continue_candidate_search(self):
        for name in ("dye.cpp", "equipment.cpp"):
            source = (SRC / name).read_text(encoding="utf-8")
            fallback = body(source, "uintptr_t FindCharacterFallback(")
            self.assertIn("FindTrackedCharacterComp", fallback)
            self.assertIn("ProfileComp", fallback)
            self.assertNotIn("Inventory::CharacterAddrs", fallback)
            for signature in ("uintptr_t ClientComp(", "uintptr_t ServerComp("):
                self.assertNotIn("Inventory::CharacterAddr(", body(source, signature))
            table_reader = body(source, "bool ReadEquipTable(" if name == "dye.cpp"
                                else "EquipTableDesc ReadEquipTableDesc(")
            self.assertIn("ReadNativeEquipmentTable(comp)", table_reader)
            scan = body(source, "uintptr_t FindEquipCompFromActor(")
            self.assertIn("AcceptCharacterComponent(targetIdx", scan)
            self.assertNotIn("subOffsets", scan)

    def test_refinement_is_queued_and_does_not_call_wrong_engine_function(self):
        source = (SRC / "equipment.cpp").read_text(encoding="utf-8")
        edit = body(source, "bool Equipment::SetRefine(")
        self.assertIn("QueueRefine(req)", edit)
        self.assertNotIn("Write16(", edit)
        self.assertNotIn("SyncRefineAllRealms(", edit)
        self.assertIn("RefineStampCurrent", body(source, "bool SyncRefineAllRealms("))
        tick = body(source, "void Equipment::Tick(")
        self.assertIn("ProcessPendingRefines();", tick)
        self.assertNotIn("s_savedEquipSlots", tick)
        self.assertLess(tick.index("revision >= 2800"), tick.index("g_refresh("))
        holders = body(self.inventory, "int Inventory::FindAndApplyAllHolders(")
        self.assertIn("inst == targetInstId &&", holders)
        self.assertNotIn("SnapshotCandidates", holders)
        self.assertNotIn("off <= 0x200", holders)
        for signature in ("bool Inventory::RefineAllBagEquipment(", "bool Inventory::UnlockAllBagSockets("):
            bulk = body(self.inventory, signature)
            self.assertNotIn("SnapshotCandidates", bulk)
            self.assertNotIn("off <= 0x200", bulk)
            self.assertIn("IsTransactionActive()", bulk)

    def test_socket_operations_preserve_allocation_metadata(self):
        source = (SRC / "equipment.cpp").read_text(encoding="utf-8")
        for signature in ("bool WriteSocketToEntry(", "bool OpenAllSockets(", "bool EmptyAllSockets("):
            operation = body(source, signature)
            self.assertTrue("ReadWritableSocketDesc(entry)" in operation or
                            "UnlockSocketLayout" in operation)
            self.assertNotIn("sizeOff", operation)
            self.assertNotIn("capOff", operation)
        desc = body(source, "SocketDesc ReadSocketDesc(")
        self.assertIn("ReadSocketLayout", desc)
        layout = (SRC / "socket_layout.h").read_text(encoding="utf-8")
        self.assertIn("mem::Read8(out.unlockAddress", layout)
        self.assertIn("mem::Write8(layout.unlockAddress", layout)
        self.assertNotIn("Write32(", layout)

    def test_dye_requires_client_render_target(self):
        source = (SRC / "dye.cpp").read_text(encoding="utf-8")
        render = body(source, "bool IsRenderComp(")
        self.assertIn("IsNativeClientEquip(comp)", render)
        self.assertIn("!IsValidCanonicalPtr(context)", render)
        apply = body(source, "bool CallDyeApply(")
        self.assertIn("!MatchingDyeEntry(comp, item)", apply)
        request = body(source, "void ProcessRequest(")
        self.assertIn("ResolveDyeRenderTarget(", request)
        self.assertNotIn("applyComps[10]", request)
        self.assertIn("return false;", body(source, "bool CallDyeApplySlot("))

    def test_dye_mirror_and_selection_preserve_target(self):
        source = (SRC / "dye.cpp").read_text(encoding="utf-8")
        mirror = body(source, "bool MirrorToServer(")
        self.assertNotIn("s_activeCharIdx", mirror)
        self.assertNotIn("s_targetMode", mirror)
        self.assertIn("MatchingDyeEntry(comp, item)", mirror)
        self.assertNotIn("Write32(", mirror)
        for signature in ("void Dye::SetActiveCharacter(", "void Dye::SetTargetMode(", "void Dye::SetActiveMount("):
            setter = body(source, signature)
            self.assertIn(".exchange(", setter)
            self.assertIn("s_selectionGeneration.fetch_add", setter)
            self.assertNotIn("operation.acquired", setter)
        matching = body(source, "uintptr_t MatchingDyeEntry(")
        self.assertIn("item.instance > 0 && instance == item.instance", matching)
        self.assertIn("comp == item.sourceComp", matching)

    def test_ui_snapshot_and_background_work_are_bounded(self):
        equipment = (SRC / "equipment.cpp").read_text(encoding="utf-8")
        dye = (SRC / "dye.cpp").read_text(encoding="utf-8")
        self.assertIn("EnsureSnapshot();", body(equipment, "bool Equipment::Ready("))
        self.assertIn("EnsureSnapshot();", body(equipment, "int Equipment::SlotCount("))
        self.assertIn("kSnapshotIntervalMs", body(equipment, "void EnsureSnapshot("))
        self.assertNotIn("return false;", body(equipment, "bool RebuildSnapshot(").split("const SocketDesc sockets", 1)[1].split("for (auto& socket", 1)[0])
        tick = body(dye, "void Dye::Tick(")
        self.assertIn("kRestoreTickMs = 100", tick)
        self.assertIn("if (selected < 0) return;", tick)
        self.assertNotIn("for (int c = 0; c < 3; ++c)", tick)
        self.assertIn("kProfileScanBudget = 64", self.player)
        profile = body(self.player, "static uintptr_t FindProfileEquipComp(")
        self.assertNotIn("subOffsets", profile)

    def test_nonlethal_context_is_used_before_amplification(self):
        damage = body(self.player, "int64_t __fastcall hkDamageApply(")
        self.assertIn("ContextForDispatch", damage)
        classified = body(self.player, "int64_t ApplyClassifiedDamage(")
        self.assertIn("damage_policy::ApplyOutgoing", classified)
        self.assertNotIn("2000000000", damage)
        self.assertNotIn("Write64", damage)
        context = body(self.player, "damage_policy::HitContext ReadDamageContext(")
        self.assertIn("definition + 0x118", context)
        self.assertIn("g_damageBuffVtable", context)
        self.assertIn("__finally", body(self.player, "void* InvokeDamageEvent("))

    def test_background_scans_and_damage_hooks_do_not_do_exhaustive_resolve(self):
        resolve = body(self.player, "void TickResolveSelf(")
        self.assertIn("cellCount", resolve)
        self.assertNotIn("i < count", resolve)
        mount = body(self.player, "uintptr_t FindMountEquipComp(")
        self.assertIn("ReadNativeEquipmentTable", mount)
        self.assertNotIn("subOffsets", mount)
        self.assertNotIn("tableOffsets", mount)
        self.assertNotIn("TickResolveSelf", body(self.player, "int64_t ApplyClassifiedDamage("))
        for signature in ("void TrackInventoryChanges(", "void RepairInventorySlice("):
            scan = body(self.inventory, signature)
            self.assertIn("scan.Slice", scan)
            self.assertIn("g_inventoryMutation", scan)
        tick = body(self.inventory, "static void InventoryTickImpl(")
        self.assertNotIn("RepairUsedSlots(", tick)

    def test_equipment_sync_requires_distinct_realms_and_queues_sockets(self):
        equipment = (SRC / "equipment.cpp").read_text(encoding="utf-8")
        sync = body(equipment, "bool SyncRefineAllRealms(")
        self.assertIn("FindClientEquipmentReplica", sync)
        self.assertIn("clientWritten && serverWritten", sync)
        self.assertNotIn("FindAndApplyAllHolders", sync)
        self.assertNotIn("RealmFlag", sync)
        for signature in ("bool Equipment::AddGear(", "bool Equipment::ClearGear(",
                          "bool Equipment::UnlockAll(", "bool Equipment::ClearAll("):
            edit = body(equipment, signature)
            self.assertIn("QueueEquipmentEdit", edit)
            self.assertNotIn("SyncSocketAllRealms", edit)
            self.assertNotIn("SyncSlotToProfile", edit)

    def test_overlay_fences_submission_and_upload_ring_together(self):
        overlay = (SRC.parent / "hooks" / "dx12_hook.cpp").read_text(encoding="utf-8")
        draw = body(overlay, "static void DrawOverlay(")
        self.assertIn("g_submissions[OverlaySubmissionSlot(g_overlaySubmitted)]", draw)
        self.assertLess(draw.index("WaitForOverlayFence"), draw.index("submission.commandAllocator->Reset()"))
        self.assertLess(draw.index("WaitForOverlayFence"), draw.index("ImGui_ImplDX12_RenderDrawData"))
        self.assertNotIn("frame.fenceValue", draw)
        self.assertIn("if (!retired)", draw)
        self.assertIn("g_renderDisabled = true", draw)
        self.assertIn("++g_overlaySubmitted", draw)
        self.assertIn("g_device, kOverlayFramesInFlight,", body(overlay, "static bool InitImGui("))
        self.assertNotIn("g_submissions", body(overlay, "static bool ResizeFrameResources("))
        self.assertIn("drawData->TotalVtxCount == 0", draw)

    def test_equipment_retries_do_not_force_render_rebuild_or_game_thread_io(self):
        equipment = (SRC / "equipment.cpp").read_text(encoding="utf-8")
        pump = body(equipment, "void ProcessPendingRefines(")
        self.assertNotIn("if (componentWrites || holderWrites) InvalidateSnapshot", pump)
        self.assertIn("if (req.changedThisVisit) InvalidateSnapshot();", pump)
        snapshot = body(equipment, "void EnsureSnapshot(")
        self.assertIn("if (g_snapshotAttempted && now - g_lastSnapshotAttempt < kSnapshotIntervalMs) return;", snapshot)
        self.assertNotIn("epoch == g_observedSnapshotEpoch", snapshot)
        sync = body(equipment, "bool SyncRefineAllRealms(")
        self.assertIn("SliceWhile", sync)
        self.assertIn("MutationEpoch() != epoch", sync)
        self.assertIn("req.holders[0].complete && req.holders[1].complete", sync)
        self.assertNotIn("SaveEquipProfilesToDisk();", body(equipment, "void Equipment::Tick("))
        self.assertIn("std::async", body(equipment, "static void PumpEquipProfileSave("))
        writer = body(equipment, "static bool WriteEquipProfiles(")
        self.assertNotIn("g_profileMutex", writer)
        self.assertNotIn("Player::", writer)
        self.assertIn("g_profileSaveWorker.get()", body(equipment, "void Equipment::Remove("))

    def test_dye_restore_uses_payload_comparison_and_partial_visual_progress(self):
        dye = (SRC / "dye.cpp").read_text(encoding="utf-8")
        tick = body(dye, "void Dye::Tick(")
        self.assertNotIn("memcmp(liveRecs", tick)
        self.assertNotIn("memcmp(renderRecs", tick)
        self.assertIn("SameDyePayload", tick)
        restore = body(dye, "static uint32_t RestoreApplySlot(")
        self.assertIn("mask = FirstDyeChannel(mask)", restore)
        self.assertIn("state.pending &= ~painted", tick)
        upsert = body(dye, "bool CallDyeUpsert(")
        self.assertLess(upsert.index("SameDyePayload"), upsert.index("g_dyeUpsert(reinterpret_cast"))

    def test_mount_detection_shares_native_local_slot_contract(self):
        dye = (SRC / "dye.cpp").read_text(encoding="utf-8")
        player_mount = body(self.player, "uintptr_t FindMountEquipComp(")
        self.assertIn("NativeMountEquipmentFromRoot", player_mount)
        self.assertIn("ReadNativeMountGear", player_mount)
        self.assertIn("ReadNativeMountGear", body(dye, "bool CompHasHorseGear("))
        self.assertIn("NativeMountSlotName(tag)", body(dye, "bool CopySnapshotRow("))
        resolve = body(dye, "uintptr_t FindMountComp(")
        self.assertIn("roots[] = {trackedOwner, trackedActor}", resolve)
        self.assertIn("NativeMountEquipmentFromRoot(root)", resolve)
        self.assertNotIn("PreferEquipmentOwner", resolve)
        self.assertIn("FindMountComp(item.mount) != item.sourceComp", body(dye, "bool SourceItemValid("))
        equipment = (SRC / "equipment.cpp").read_text(encoding="utf-8")
        repair = body(equipment, "bool Equipment::RepairAll(")
        self.assertLess(repair.index("durability >= 10000"), repair.index("Inventory::NameForTypeId"))
        self.assertIn("scomp != comp", repair)
        self.assertIn("profC != comp && profC != scomp", repair)
        self.assertIn("FindEntryByTag(comp, req.tag, false)", body(equipment, "uintptr_t MatchingRefineEntry("))

    def test_named_dye_profiles_capture_and_apply_stamped_native_item(self):
        dye = (SRC / "dye.cpp").read_text(encoding="utf-8")
        queue = body(dye, "bool EnqueueDye(")
        self.assertIn("DyeProfileCompatible", queue)
        self.assertIn("req.item.sourceComp != displayed.sourceComp", queue)
        self.assertIn("RequestStillValid(req)", queue)
        request = body(dye, "void ProcessRequest(")
        capture = request.split("if (req.saveProfile)", 1)[1].split("DyeRealmGuard clientRealm", 1)[0]
        self.assertIn("ReadRecords(item.sourceEntry", capture)
        self.assertIn("RequestStillValid(req)", capture)
        self.assertIn("std::async", capture)
        self.assertNotIn("CallDye", capture)
        self.assertNotIn("Upsert", capture)
        self.assertIn("ProfileReadbackMatches(req, entry)", request)
        readback = body(dye, "bool ProfileReadbackMatches(")
        self.assertIn("mask != 0xFFFu", readback)
        self.assertIn("SameDyePayload", readback)
        record = body(dye, "void BuildRequestRecord(")
        self.assertIn("DyeProfileRecord", record)
        tick = body(dye, "void Dye::Tick(")
        self.assertIn("s_profileSave.wait_for(std::chrono::seconds(0))", tick)

    def test_stamina_rebinds_client_stats_and_honors_native_count(self):
        resolve = body(self.player, "void TickResolveSelf(")
        self.assertNotIn("kStatArray_ScanEntries", resolve)
        self.assertIn("mainC.count", resolve)
        self.assertIn("nativePartyBody = sharesPlayerPoss && isPlayerOrCompTag", resolve)
        self.assertIn("i < nPlayers", resolve)
        self.assertIn("Read16", body(self.player, "bool StatEntryType("))
        self.assertIn("ReadStatView(owner)", body(self.player, "bool WalkSelfChain("))
        self.assertIn("TrackedStatEntry(e)", body(self.player, "void PinEntry("))
        client = body(self.player, "void RefreshClientStats(")
        self.assertIn("ReadActorRegistry", client)
        self.assertIn("nodes < 64", client)
        self.assertIn("IsNativeClientEquip", client)
        self.assertIn("st.infMountStamina || st.infStamina", client)
        self.assertIn("st.godMode && IsHealthType(type)", client)
        self.assertIn("RefreshClientStats();", body(self.player, "void Player::TickImpl("))
        self.assertIn("__finally", body(self.player, "void Player::Tick("))
        for signature in ("static int IdentifyCharacterIdentity(", "int Inventory::IdentifyCharacterFromEquip("):
            identity = body(self.inventory, signature)
            self.assertIn("core::GetGameVersion().revision < 2800", identity)

    def test_mount_dye_resolves_client_renderer_and_slices_channels(self):
        dye = (SRC / "dye.cpp").read_text(encoding="utf-8")
        resolve = body(dye, "DyeRenderTarget ResolveDyeRenderTarget(")
        self.assertIn("s_mountRenderers[item.mount]", resolve)
        self.assertIn("item.instance <= 0", resolve)
        self.assertIn("!mount && ReadPtr(owner + kOff_Owner_Possessor", resolve)
        self.assertIn("IsNativeMountOwner(owner)", body(dye, "DyeRenderTarget RenderTargetFromOwner("))
        mount = body(dye, "void ProcessRequest(").split("// ===== MOUNT MODE", 1)[1].split("// ===== PLAYER CHARACTER", 1)[0]
        self.assertIn("CallDyeVisualSet", mount)
        self.assertIn("CallDyeVisualClear", mount)
        self.assertNotIn("CallDyeApply(", mount)
        self.assertNotIn("TriggerEquipMeshRebuild", mount)
        self.assertIn("const int chLast = chFirst", mount)
        self.assertIn("RequestStillValid(req)", mount)
        self.assertIn("g_req.nextVisit", body(dye, "void Dye::Tick("))
        replay = body(dye, "void Dye::Tick(").split("// Tracked mounts share", 1)[1]
        self.assertIn("DyeRealmGuard clientRealm(0)", replay)
        self.assertLess(replay.index("DyeRealmGuard clientRealm(0)"), replay.index("CallDyeVisualSet"))


if __name__ == "__main__":
    unittest.main()
