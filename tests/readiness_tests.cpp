#include "../src/core/readiness.h"
#include "../src/core/build_timestamp.h"
#include "../src/core/version_mapping.h"
#include "../src/core/startup_notice.h"
#include "../src/game/crime_hook_contract.h"
#include "../src/game/inventory_hook_contract.h"
#include "../src/game/inventory_logic.h"
#include "../src/game/player_logic.h"
#include "../src/game/equipment_logic.h"
#include "../src/game/worker_logic.h"
#include "../src/game/offsets.h"
#include "../src/mem/section_filter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace trinity::game
{
    int64_t ScaleTrustValue(int64_t previous, bool hasPrevious,
                            int64_t incoming, float multiplier, bool enabled);
    bool SelectTrustBaseline(int64_t stored, bool hasStored,
                             int64_t cached, bool hasCached,
                             int64_t* outBaseline);

    bool IsAuthoritativeHolderCandidate(uintptr_t clientContainer,
                                        uintptr_t clientHolder,
                                        uintptr_t candidateContainer,
                                        uintptr_t candidateHolder,
                                        bool candidateIsLiveCharacter,
                                        uint32_t clientBucketCount,
                                        uint32_t candidateBucketCount);

}

static_assert(std::is_same_v<trinity::game::RegisterCrimeEvent_t,
                             void(__fastcall*)(void*, const char*, void*, void*)>,
              "crime-event forwarding must preserve the full 64-bit string pointer");
static_assert(std::is_same_v<trinity::game::InventoryCommit201_t,
                             void*(__fastcall*)(void*, void*, void*, uint16_t,
                                                void*, uint8_t, uint8_t, uint8_t)>,
              "TU 2.01 inventory commit hook must forward all eight arguments");

namespace
{
    int failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (condition) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }

    void DelayedCodeIsRetriedUntilReady()
    {
        uint64_t now = 0;
        int probes = 0;
        int pauses = 0;

        const bool ready = trinity::core::WaitForReadiness(
            [&] { return ++probes == 3; },
            [&] { return now; },
            [&](uint32_t ms) { now += ms; ++pauses; },
            5000, 250);

        Expect(ready, "delayed code should become ready");
        Expect(probes == 3, "readiness must be probed until the third successful attempt");
        Expect(pauses == 2, "the wait must pause only between failed probes");
        Expect(now == 500, "retry interval must be applied exactly");
    }

    void MissingCodeTimesOutWithoutAnExtraFullSleep()
    {
        uint64_t now = 0;
        int probes = 0;
        int pauses = 0;

        const bool ready = trinity::core::WaitForReadiness(
            [&] { ++probes; return false; },
            [&] { return now; },
            [&](uint32_t ms) { now += ms; ++pauses; },
            600, 250);

        Expect(!ready, "permanently missing code must time out");
        Expect(probes == 4, "timeout boundary must receive one final readiness probe");
        Expect(pauses == 3, "timeout should use two full pauses and one bounded remainder");
        Expect(now == 600, "the final pause must be clamped to the remaining timeout");
    }

    void PeRevisionMapsToCurrentTitleUpdate()
    {
        using trinity::core::ModernTitleUpdateForRevision;

        Expect(std::strcmp(ModernTitleUpdateForRevision(2760), "2.01.00") == 0,
               "PE revision 2760 must identify TU 2.01.00");
        Expect(std::strcmp(ModernTitleUpdateForRevision(2850), "2.02.00") == 0,
               "PE revision 2850 must identify TU 2.02.00");
        Expect(std::strcmp(ModernTitleUpdateForRevision(2944), "PE 2944") == 0,
               "PE revision 2944 must display its confirmed executable identity without guessing a TU name");
        const char* const pe2949Title = ModernTitleUpdateForRevision(2949);
        Expect(pe2949Title && std::strcmp(pe2949Title, "2.03.01") == 0,
               "PE revision 2949 must identify the user-confirmed 2.03.01 update");
        Expect(std::strcmp(ModernTitleUpdateForRevision(2692), "2.00.02") == 0,
               "PE revision 2692 must identify TU 2.00.02");
        Expect(std::strcmp(ModernTitleUpdateForRevision(2658), "2.00.01") == 0,
               "PE revision 2658 must remain TU 2.00.01");
        Expect(std::strcmp(ModernTitleUpdateForRevision(2625), "2.00.00") == 0,
               "PE revision 2625 must remain TU 2.00.00");
        Expect(ModernTitleUpdateForRevision(2761) == nullptr,
               "an unrecognised newer PE revision must not be labelled as a known title update");
        Expect(ModernTitleUpdateForRevision(2851) == nullptr,
               "a revision after TU 2.02.00 must not inherit a confirmed label without evidence");
        Expect(ModernTitleUpdateForRevision(2474) == nullptr,
               "pre-2.00 revisions must keep using binary fingerprinting");
    }

    void CurrentUpdateUsesCompatibleReadinessProfile()
    {
        using trinity::core::ReadinessProfile;
        using trinity::core::ReadinessProfileForRevision;

        Expect(ReadinessProfileForRevision(2760) == ReadinessProfile::Tu201KnownCompatible,
               "PE revision 2760 must not wait for removed TU 2.00.02 signatures");
        Expect(ReadinessProfileForRevision(2850) == ReadinessProfile::Tu201KnownCompatible,
               "PE revision 2850 must use the verified modern readiness probes");
        Expect(ReadinessProfileForRevision(2944) == ReadinessProfile::Tu201KnownCompatible,
               "PE revision 2944 must not wait for the removed stat-commit sentinel");
        Expect(ReadinessProfileForRevision(2949) == ReadinessProfile::Tu201KnownCompatible,
               "PE revision 2949 must use its byte-verified modern readiness probes");
        Expect(ReadinessProfileForRevision(2692) == ReadinessProfile::LegacyComplete,
               "PE revision 2692 must retain the complete TU 2.00.02 readiness profile");
    }

    void MovementOwnerOffsetTracksCurrentLayout()
    {
        using trinity::core::MoveComponentOwnerOffsetForRevision;

        Expect(MoveComponentOwnerOffsetForRevision(2760) == 0x2B8,
               "PE revision 2760 locomotion component must use move-owner offset 0x2B8");
        Expect(MoveComponentOwnerOffsetForRevision(2850) == 0x2B8,
               "PE revision 2850 locomotion component must retain the live-verified move-owner offset 0x2B8");
        Expect(MoveComponentOwnerOffsetForRevision(2944) == 0x2C0,
               "PE 2944 must use the live-observed move-owner offset 0x2C0 so Super Run/Free Flight can identify the local player");
        Expect(MoveComponentOwnerOffsetForRevision(2949) == 0x2C0,
               "PE 2949 must retain the byte-verified PE 2944 move-owner offset 0x2C0");
        Expect(MoveComponentOwnerOffsetForRevision(2692) == 0x298,
               "pre-2.01 locomotion components must retain move-owner offset 0x298");
        Expect(MoveComponentOwnerOffsetForRevision(2945) == 0,
               "an unknown newer PE revision must not inherit a locomotion owner offset");
    }

    void Pe2944LocoStepperContractIsExplicit()
    {
        using trinity::core::LocoStepperContract;
        using trinity::core::LocoStepperContractForRevision;

        Expect(LocoStepperContractForRevision(2944) == LocoStepperContract::Pe2944,
               "PE 2944 must select its exact live-observed locomotion-stepper contract");
        Expect(LocoStepperContractForRevision(2949) == LocoStepperContract::Pe2944,
               "PE 2949 must select the byte-verified PE 2944 locomotion-stepper contract");
        Expect(LocoStepperContractForRevision(2850) == LocoStepperContract::Modern,
               "PE 2850 must keep its separately verified modern locomotion contract");
        Expect(LocoStepperContractForRevision(2945) == LocoStepperContract::Unsupported,
               "an unknown newer PE revision must not inherit a locomotion-stepper signature");
    }

    void CurrentUpdateRejectsLegacyFuzzySignatures()
    {
        using trinity::core::MayUseLegacyFuzzySignaturesForRevision;

        Expect(!MayUseLegacyFuzzySignaturesForRevision(2760),
               "PE revision 2760 must not resolve native calls through broad legacy signatures");
        Expect(!MayUseLegacyFuzzySignaturesForRevision(2761),
               "an unknown newer revision must fail closed instead of using legacy fuzzy signatures");
        Expect(MayUseLegacyFuzzySignaturesForRevision(2692),
               "TU 2.00.02 must retain its legacy compatibility fallbacks");
    }

    void Pe2944SkipsTheObsoleteCrimeEventDispatcherProbe()
    {
        using trinity::core::MayProbeLegacyCrimeEventDispatcherForRevision;

        Expect(!MayProbeLegacyCrimeEventDispatcherForRevision(2944),
               "PE 2944 must not probe the removed legacy crime dispatcher or emit a misleading missing-signature error");
        Expect(!MayProbeLegacyCrimeEventDispatcherForRevision(2945),
               "an unknown newer revision must not inherit the legacy crime-dispatcher probe");
        Expect(MayProbeLegacyCrimeEventDispatcherForRevision(2760),
               "the verified legacy dispatcher probe must remain available to its original PE family");
    }

    void Tu202UsesOnlyTheConfirmedModernInventoryContract()
    {
        using trinity::core::UsesTu201CompatibleRevision;

        Expect(UsesTu201CompatibleRevision(2760),
               "TU 2.01 must retain its modern inventory contract");
        Expect(UsesTu201CompatibleRevision(2850),
               "TU 2.02 must select the verified modern inventory contract");
        Expect(UsesTu201CompatibleRevision(2944),
               "PE 2944's live-audited modern transaction primitives must select the current inventory ABI");
        Expect(UsesTu201CompatibleRevision(2949),
               "PE 2949's unique PE 2944 transaction primitives must select the modern inventory ABI");
        Expect(!UsesTu201CompatibleRevision(2692),
               "TU 2.00.02 must not be routed through the newer inventory ABI");
        Expect(!UsesTu201CompatibleRevision(2851),
               "an unverified newer revision must not inherit TU 2.02 ABI selection");
        Expect(!UsesTu201CompatibleRevision(2945),
               "a revision after PE 2944 must not inherit its audited ABI selection");
    }

    void Pe2949UsesTheNativeSlotExpansionSetter()
    {
        using trinity::core::SlotSizeOverrideSupportedForRevision;

        Expect(SlotSizeOverrideSupportedForRevision(2949),
               "PE 2949 must enable Slot Size only through its native expansion setter");
        Expect(SlotSizeOverrideSupportedForRevision(2944),
               "PE 2944 must retain its established Slot Size contract");
        Expect(SlotSizeOverrideSupportedForRevision(2850),
               "PE 2850 must retain its established Slot Size contract");
        Expect(std::strstr(trinity::game::kSig_InvSetExpandSlots2949,
                           "48 8B 41 18 41 0F B7 E9 8B 49 20 4C 8B F2 4C 8D 14 C8") != nullptr,
               "PE 2949 must bind the audited four-argument native setter");
    }

    void Pe2949PickupPatchOnlyTransitionsBetweenExactInstructionStates()
    {
        using trinity::game::PickupCapacityPatchState;
        const uint8_t original[] = { 0x74, 0x07 };
        const uint8_t patched[]  = { 0x90, 0x90 };
        const uint8_t altered[]  = { 0x75, 0x07 };

        Expect(trinity::game::CanTransitionPickupCapacityPatch(
                   PickupCapacityPatchState::Original, PickupCapacityPatchState::Patched,
                   original, original, sizeof(original)),
               "PE 2949 Slot Size may enable only the verified pickup branch patch");
        Expect(trinity::game::CanTransitionPickupCapacityPatch(
                   PickupCapacityPatchState::Patched, PickupCapacityPatchState::Original,
                   patched, patched, sizeof(patched)),
               "PE 2949 Slot Size must restore the original pickup branch on disable");
        Expect(!trinity::game::CanTransitionPickupCapacityPatch(
                   PickupCapacityPatchState::Original, PickupCapacityPatchState::Patched,
                   altered, original, sizeof(altered)),
               "unexpected pickup branch bytes must fail closed");
    }

    void InventoryRootAnchorTracksCurrentInstructionLayout()
    {
        using trinity::core::InventoryCoreGlobalMovOffsetForRevision;

        Expect(InventoryCoreGlobalMovOffsetForRevision(2760) == 0,
               "TU 2.01 inventory-root signature must resolve RIP at the match start");
        Expect(InventoryCoreGlobalMovOffsetForRevision(2850) == 0,
               "TU 2.02 inventory-root signature must resolve RIP at the verified match start");
        Expect(InventoryCoreGlobalMovOffsetForRevision(2692) == 0x15,
               "pre-2.01 inventory-root signature must retain its legacy mov offset");
    }

    void RealmFlagOffsetTracksCurrentTlsLayout()
    {
        using trinity::core::RealmFlagOffsetForRevision;

        Expect(RealmFlagOffsetForRevision(2760) == 0x1FD,
               "TU 2.01 realm selection must use the new TLS byte at +0x1FD");
        Expect(RealmFlagOffsetForRevision(2850) == 0x1EC,
               "TU 2.02 realm selection must use tls+0x1EC (live-verified: 0x1FD is "
               "non-boolean there, causes planner error -771604600)");
        Expect(RealmFlagOffsetForRevision(2944) == 0x1EC,
               "PE 2944's live realm-selector probe must use tls+0x1EC");
        Expect(RealmFlagOffsetForRevision(2949) == 0x1EC,
               "PE 2949 must retain the byte-verified tls+0x1EC realm selector");
        Expect(RealmFlagOffsetForRevision(2692) == 0x1F2,
               "pre-2.01 builds must retain the legacy TLS byte at +0x1F2");
    }

    void TrustScalingUsesTheFirstPositiveGain()
    {
        using trinity::game::ScaleTrustValue;

        Expect(ScaleTrustValue(0, false, 5, 25.0f, true) == 100,
               "a first +5 trust event at x25 must immediately reach 100");
        Expect(ScaleTrustValue(20, true, 25, 3.0f, true) == 35,
               "an existing +5 trust event at x3 must add 15 to the old value");
        Expect(ScaleTrustValue(20, true, 15, 25.0f, true) == 15,
               "trust losses must pass through without multiplication");
        Expect(ScaleTrustValue(20, true, 25, 25.0f, false) == 25,
               "disabled trust scaling must leave the incoming value unchanged");
    }

    void CachedTrustBaselineWinsOverAliasedLiveRecord()
    {
        using trinity::game::SelectTrustBaseline;

        int64_t baseline = -1;
        Expect(SelectTrustBaseline(5, true, 0, true, &baseline) && baseline == 0,
               "cached pre-write trust must win when the live source aliases the destination");
        Expect(SelectTrustBaseline(20, true, 0, false, &baseline) && baseline == 20,
               "live-map trust must seed the cache when no prior observation exists");
        Expect(!SelectTrustBaseline(0, false, 0, false, &baseline),
               "missing cache and live-map state must report no baseline");
    }

    void AddItemRequiresAnAuthoritativeServerHolder()
    {
        using trinity::game::CanCommitAuthoritativeAdd;

        Expect(!CanCommitAuthoritativeAdd(true, true, 0x1000, 0),
               "client-only add must fail closed instead of creating a ghost item");
        Expect(!CanCommitAuthoritativeAdd(true, true, 0x1000, 0x1000),
               "one holder cannot stand in for both client and server authority");
        Expect(CanCommitAuthoritativeAdd(true, true, 0x1000, 0x2000),
               "distinct client and server holders may commit an authoritative add");
    }

    void AddItemRetriesOnlyWhileAuthorityIsMissing()
    {
        using trinity::game::ShouldRetryAuthoritativeAdd;

        Expect(ShouldRetryAuthoritativeAdd(true, true, 0x1000, 0, 0, 120),
               "a ready add may wait for the server holder");
        Expect(!ShouldRetryAuthoritativeAdd(true, true, 0x1000, 0, 120, 120),
               "an authority wait must stop at the retry limit");
        Expect(!ShouldRetryAuthoritativeAdd(true, true, 0x1000, 0x2000, 0, 120),
               "a complete authority pair must commit instead of retrying");
        Expect(!ShouldRetryAuthoritativeAdd(false, true, 0x1000, 0, 0, 120),
               "an incomplete engine path must fail instead of retrying forever");
    }

    void AuthoritativeHolderCaptureRejectsUnsafeCandidates()
    {
        using trinity::game::IsAuthoritativeHolderCandidate;

        Expect(!IsAuthoritativeHolderCandidate(0x1000, 0x2000, 0x1000, 0x3000,
                                                true, 45, 45),
               "the client container must never be reused as server authority");
        Expect(!IsAuthoritativeHolderCandidate(0x1000, 0x2000, 0x3000, 0x4000,
                                                false, 45, 45),
               "an unpossessed planner copy must not become server authority");
        Expect(!IsAuthoritativeHolderCandidate(0x1000, 0x2000, 0x3000, 0x4000,
                                                true, 45, 46),
               "a different bucket layout must not become server authority");
        Expect(IsAuthoritativeHolderCandidate(0x1000, 0x2000, 0x3000, 0x4000,
                                              true, 45, 45),
               "a distinct live player container with matching buckets is authoritative");
    }

    void PassiveHolderCandidatePreFilterRejectsContention()
    {
        using trinity::game::ShouldInspectPassiveHolderCandidate;

        Expect(!ShouldInspectPassiveHolderCandidate(0x4000, 0x1000, 0x2000, 0x3000, 45, 45),
               "already resolved server holder must fast-exit without inspection");
        Expect(!ShouldInspectPassiveHolderCandidate(0, 0, 0x2000, 0x3000, 45, 45),
               "null container must be rejected");
        Expect(!ShouldInspectPassiveHolderCandidate(0, 0x1000, 0x3000, 0x3000, 45, 45),
               "client holder must be rejected without locking");
        Expect(!ShouldInspectPassiveHolderCandidate(0, 0x1000, 0x2000, 0x3000, 45, 2),
               "mismatched bucket count (NPC or chest) must be rejected without locking");
        Expect(ShouldInspectPassiveHolderCandidate(0, 0x1000, 0x2000, 0x3000, 45, 45),
               "valid distinct candidate with matching buckets must be inspected");
    }

    void GodModeRequiresStrictPlayerTarget()
    {
        using trinity::game::ShouldBlockPlayerDamage;

        Expect(ShouldBlockPlayerDamage(false, true, false) == false,
               "God Mode off must never block damage");
        Expect(ShouldBlockPlayerDamage(true, false, false) == false,
               "God Mode must not block damage to an unclassified enemy");
        Expect(ShouldBlockPlayerDamage(true, true, false),
               "God Mode blocks damage to a strict player target");
        Expect(ShouldBlockPlayerDamage(true, false, true),
               "God Mode blocks damage to a tracked mount");
    }

    void IdentifiedEquipmentWinsOverPartySlot()
    {
        using trinity::game::AcceptCharacterComponent;

        Expect(!AcceptCharacterComponent(2, 1, 2),
               "a Damiane gear identity must not be routed to the selected Oongka");
        Expect(AcceptCharacterComponent(2, 2, 1),
               "a matching Oongka gear identity must win over party position");
        Expect(AcceptCharacterComponent(2, -1, 2),
               "an unidentified selected party actor remains usable");
        Expect(!AcceptCharacterComponent(2, 1, 1),
               "a different party actor cannot be used for Oongka");
    }

    void EquipmentLookupUsesTheOwningCharacter()
    {
        using trinity::game::PreferEquipmentOwner;

        Expect(PreferEquipmentOwner(0x22000000, 0x33000000) == 0x22000000,
               "equipment lookup must start from the owner that owns the equip component");
        Expect(PreferEquipmentOwner(0, 0x33000000) == 0x33000000,
               "the inner actor remains a fallback when no owner was captured");
    }

    void MountClassificationUsesTheTypeDescriptorTag()
    {
        using trinity::game::IsMountTypeTag;

        Expect(IsMountTypeTag(5), "vehicle descriptor tag must identify a mount");
        Expect(IsMountTypeTag(6), "pet descriptor tag must identify a mount candidate");
        Expect(!IsMountTypeTag(4), "mercenary descriptor tag must not identify a mount");
        Expect(!IsMountTypeTag(1), "player descriptor tag must not identify a mount");
    }

    void VehiclesDoNotConsumeTheOongkaTrackingSlot()
    {
        using trinity::game::IsTrackedProtagonistTypeTag;

        // Live TU 2.01 order observed in the character manager:
        // Kliff, Damiane, horse, pet, Oongka.
        constexpr uint8_t tags[] = { 1, 4, 5, 6, 4 };
        int selected[3] = { -1, -1, -1 };
        int count = 0;
        for (int i = 0; i < 5 && count < 3; ++i)
        {
            if (IsTrackedProtagonistTypeTag(tags[i]))
                selected[count++] = i;
        }

        Expect(count == 3, "all three protagonists must remain trackable");
        Expect(selected[0] == 0 && selected[1] == 1 && selected[2] == 4,
               "horse and pet entries must not displace Oongka from the three protagonist slots");
    }

    void EquipmentDemandRefreshesCharactersWithoutStatCheats()
    {
        using trinity::game::ShouldRefreshTrackedCharacters;

        Expect(ShouldRefreshTrackedCharacters(false, 1000, 1500),
               "equipment demand must refresh character tracking while stat cheats are off");
        Expect(!ShouldRefreshTrackedCharacters(false, 1501, 1500),
               "expired equipment demand must not keep the character-manager scan running forever");
        Expect(ShouldRefreshTrackedCharacters(true, 1501, 0),
               "active stat features must continue refreshing characters independently");
    }

    void MissingCatalogResolverIsRetriedAfterTheInterval()
    {
        using trinity::game::ShouldAttemptCatalogResolve;

        Expect(ShouldAttemptCatalogResolve(false, 1000, 0, 1000),
               "a missing item table must receive an initial resolve attempt");
        Expect(!ShouldAttemptCatalogResolve(false, 1500, 1000, 1000),
               "catalog resolution must not rescan every rendered frame");
        Expect(ShouldAttemptCatalogResolve(false, 2000, 1000, 1000),
               "a missing item table must be retried after the interval");
        Expect(!ShouldAttemptCatalogResolve(true, 2000, 1000, 1000),
               "a resolved item table must stop further resolver scans");
    }

    void TrustRecordUsesTheCopiedValueField()
    {
        Expect(trinity::game::kOff_FriendlyRec_Value == 0x20,
               "TU 2.01 trust must read the value copied from record+0x20");
    }

    void Pe2944HolderPlannerSignatureKeepsItsOwnFrameContract()
    {
        Expect(std::strstr(trinity::game::kSig_InvHolderInsert2944,
                           "48 8D AC 24 F0 FD FF FF 48 81 EC 10 03 00 00") != nullptr,
               "PE 2944 holder planner must keep its exact recompiled frame signature");
        Expect(std::strcmp(trinity::game::kSig_InvHolderInsert2944,
                           trinity::game::kSig_InvHolderInsert201) != 0,
               "PE 2944 must not silently reuse the PE 2850 holder-planner signature");
    }

    void ExecutableDebugSectionIsScanned()
    {
        using trinity::mem::ShouldScanSection;

        Expect(ShouldScanSection(".debug", 0x20000000u),
               "TU 2.00.02 executable .debug section must be scanned");
        Expect(!ShouldScanSection(".debug", 0x40000000u),
               "non-executable stale .debug data should remain excluded");
        Expect(ShouldScanSection(".text", 0x60000020u),
               "normal executable sections must remain scannable");
    }

    void StartupNoticeIsAReadableConsoleBanner()
    {
        const auto& lines = trinity::core::StartupNoticeLines();

        Expect(lines.size() == 5,
               "startup notice must have a compact five-line console banner");
        Expect(std::strcmp(lines[0], "+======================================================================+") == 0,
               "startup notice must begin with a strong top border");
        Expect(std::strstr(lines[1], "TRINITY  |  QUICK START & SUPPORT") != nullptr,
               "startup notice must have a scannable title");
        Expect(std::strstr(lines[2], "ADD ITEM:") != nullptr,
               "startup notice must retain the Add Item setup instruction");
        Expect(std::strstr(lines[3], "https://mul0.com/trainer/crimson-desert-trinity-mod-menu/") != nullptr,
               "startup notice must retain the full support URL");
        Expect(std::strcmp(lines[4], lines[0]) == 0,
               "startup notice must close with the matching border");
        for (const char* line : lines)
            Expect(std::strlen(line) == std::strlen(lines[0]),
                   "every startup notice row must match the border width");
    }

    void BuildStampMatchesTheRequestedReleaseTimestamp()
    {
        Expect(std::strlen(TRINITY_BUILD_TIME) > 0,
               "the startup log must show the requested release build timestamp");
    }

    void WorkerPatchContractIsStrict()
    {
        const uint8_t original[] = { 0x0F, 0x85, 0x95, 0x00, 0x00, 0x00 };
        const uint8_t patched[]  = { 0xE9, 0x96, 0x00, 0x00, 0x00, 0x90 };
        const uint8_t altered[]  = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

        Expect(trinity::game::WorkerPatchSupportedForRevision(2850),
               "PE 2850 must support the supplied worker patch");
        Expect(trinity::game::WorkerPatchSupportedForRevision(2944),
               "PE 2944 must support the supplied worker patch (verified unique at 0x14214BE8C)");
        Expect(trinity::game::WorkerPatchSupportedForRevision(2949),
               "PE 2949 must support the uniquely byte-verified worker patch");
        Expect(!trinity::game::WorkerPatchSupportedForRevision(2760),
               "unrelated PE revisions must not inherit the worker patch");
        Expect(!trinity::game::WorkerPatchSupportedForRevision(0),
               "unknown PE revisions must fail closed");

        using trinity::game::WorkerPatchState;
        Expect(trinity::game::CanTransitionWorkerPatch(
                   WorkerPatchState::Original, WorkerPatchState::Patched,
                   original, original, sizeof(original)),
               "the exact original bytes must allow enabling the patch");
        Expect(trinity::game::CanTransitionWorkerPatch(
                   WorkerPatchState::Patched, WorkerPatchState::Original,
                   patched, patched, sizeof(patched)),
               "the exact patched bytes must allow restoring the original");
        Expect(trinity::game::CanTransitionWorkerPatch(
                   WorkerPatchState::Original, WorkerPatchState::Original,
                   original, original, sizeof(original)),
               "an already-original target must be an idempotent success");
        Expect(!trinity::game::CanTransitionWorkerPatch(
                    WorkerPatchState::Original, WorkerPatchState::Patched,
                    altered, original, sizeof(altered)),
               "unexpected target bytes must fail closed");
    }

    // -----------------------------------------------------------------
    // PE 2949 audit additions (2026-09-21). These lock the exact mutable
    // bytes and struct offsets that the PE 2949 native Slot Size path
    // depends on, so a future recompile cannot silently change them.
    // -----------------------------------------------------------------

    void Pe2949PickupBranchByteContractIsExact()
    {
        using namespace trinity::game;

        Expect(kInvPickupCapacityPatchSize == 2,
               "the PE 2949 pickup branch patch must be exactly the two jump bytes");
        Expect(kInvPickupCapacityOriginal[0] == 0x74 && kInvPickupCapacityOriginal[1] == 0x07,
               "the PE 2949 pickup branch original bytes must be the audited `74 07`");
        Expect(kInvPickupCapacityEnabled[0] == 0x90 && kInvPickupCapacityEnabled[1] == 0x90,
               "the PE 2949 pickup branch patch must be the audited `90 90`");
        Expect(kInvPickupCapacityOriginal[0] != kInvPickupCapacityEnabled[0],
               "the reversible pair must not collapse to identical bytes");
        Expect(std::strstr(kSig_InvPickupCapacity2949,
                           "84 D2 74 07 0F B7 4C 24 48 EB 0F") != nullptr,
               "PE 2949 must bind the pickup-planner signature whose offset +2 is the patched branch");
    }

    void Pe2949NativeSetterBucketLayoutIsExact()
    {
        using namespace trinity::game;

        // The native setter writes exactly these fields; the audit verified the
        // decoded stores in the on-disk image at RVA 0x2135850.
        Expect(kOff_InvBucket_DeltaRaw == 0x16,
               "native setter buff-accumulator field must stay at bucket+0x16");
        Expect(kOff_InvBucket_ExpandSlots == 0x1A,
               "native setter _varyExpandSlotCount must stay at bucket+0x1A");
        Expect(kOff_InvBucket_MaxSlots == 0x14,
               "the derived live capacity must stay at bucket+0x14");
        Expect(kOff_InvBucket_UsedSlots == 0x12,
               "the signed used-count compared by the pickup gate must stay at bucket+0x12");
        Expect(kOff_InvBucket_Type == 0x10,
               "bucket type selection must stay at bucket+0x10");
        Expect(kOff_InvBucket_Count == 0x08 && kOff_InvBucket_Slots == 0x00,
               "bucket slot-array layout (data at +0x00, size at +0x08) must not drift");
    }

    void Pe2949DisabledRegistryPlaceholdersResolveToNothing()
    {
        using namespace trinity::game;

        // kSig_TrItemValueDtor is intentionally empty: FindPattern() must return
        // 0 for it rather than scanning with an empty pattern. inventory.cpp
        // guards on the returned address, so this is the fail-closed contract.
        Expect(kSig_TrItemValueDtor != nullptr && kSig_TrItemValueDtor[0] == '\0',
               "the unaudited dtor entry must remain an empty placeholder, not a guessed pattern");
    }

    void Pe2949CodeSectionsAreNotNamedText()
    {
        using trinity::mem::ShouldScanSection;

        // PE 2949 ships its code in .sbss / .ecode, and its stale-data section
        // is .debug. The scanner must key on characteristics, never on the
        // traditional section names, or every signature would miss.
        constexpr uint32_t kExec = 0x20000000u;
        constexpr uint32_t kReadOnly = 0x40000000u;

        Expect(ShouldScanSection(".sbss", kExec | kReadOnly),
               "PE 2949 .sbss (the primary code section) must be scannable");
        Expect(ShouldScanSection(".ecode", kExec | kReadOnly),
               "PE 2949 .ecode must be scannable");
        Expect(ShouldScanSection(".srdata", kReadOnly),
               "PE 2949 .srdata must be scannable for string and data anchors");
        Expect(ShouldScanSection(".data1", kReadOnly),
               "PE 2949 .data1 must be scannable for data anchors");
        Expect(!ShouldScanSection(".debug", kReadOnly),
               "the non-executable stale .debug section must stay excluded");
        Expect(ShouldScanSection(nullptr, 0),
               "a section with no name must default to scannable");
    }

    void Pe2949TebRealmLayoutIsExact()
    {
        using namespace trinity::game;

        Expect(kOff_Teb_TlsPointer == 0x58,
               "the TEB ThreadLocalStoragePointer used by the realm probe must stay at +0x58");
        Expect(kTls_RealmFlag == 0x1F2,
               "the documented legacy TLS realm byte must stay at +0x1F2 so the "
               "per-revision override remains visibly separate");
        Expect(trinity::core::RealmFlagOffsetForRevision(2949) != kTls_RealmFlag,
               "PE 2949 must select its own realm byte, never the legacy default");
    }
}

int main()
{
    DelayedCodeIsRetriedUntilReady();
    MissingCodeTimesOutWithoutAnExtraFullSleep();
    PeRevisionMapsToCurrentTitleUpdate();
    CurrentUpdateUsesCompatibleReadinessProfile();
    MovementOwnerOffsetTracksCurrentLayout();
    Pe2944LocoStepperContractIsExplicit();
    CurrentUpdateRejectsLegacyFuzzySignatures();
    Pe2944SkipsTheObsoleteCrimeEventDispatcherProbe();
    Tu202UsesOnlyTheConfirmedModernInventoryContract();
    Pe2949UsesTheNativeSlotExpansionSetter();
    Pe2949PickupPatchOnlyTransitionsBetweenExactInstructionStates();
    InventoryRootAnchorTracksCurrentInstructionLayout();
    RealmFlagOffsetTracksCurrentTlsLayout();
    TrustScalingUsesTheFirstPositiveGain();
    CachedTrustBaselineWinsOverAliasedLiveRecord();
    AddItemRequiresAnAuthoritativeServerHolder();
    AddItemRetriesOnlyWhileAuthorityIsMissing();
    AuthoritativeHolderCaptureRejectsUnsafeCandidates();
    PassiveHolderCandidatePreFilterRejectsContention();
    GodModeRequiresStrictPlayerTarget();
    IdentifiedEquipmentWinsOverPartySlot();
    EquipmentLookupUsesTheOwningCharacter();
    MountClassificationUsesTheTypeDescriptorTag();
    VehiclesDoNotConsumeTheOongkaTrackingSlot();
    EquipmentDemandRefreshesCharactersWithoutStatCheats();
    MissingCatalogResolverIsRetriedAfterTheInterval();
    TrustRecordUsesTheCopiedValueField();
    Pe2944HolderPlannerSignatureKeepsItsOwnFrameContract();
    ExecutableDebugSectionIsScanned();
    StartupNoticeIsAReadableConsoleBanner();
    BuildStampMatchesTheRequestedReleaseTimestamp();
    WorkerPatchContractIsStrict();
    Pe2949PickupBranchByteContractIsExact();
    Pe2949NativeSetterBucketLayoutIsExact();
    Pe2949DisabledRegistryPlaceholdersResolveToNothing();
    Pe2949CodeSectionsAreNotNamedText();
    Pe2949TebRealmLayoutIsExact();
    if (failures == 0)
        std::puts("readiness tests passed");
    return failures == 0 ? 0 : 1;
}
