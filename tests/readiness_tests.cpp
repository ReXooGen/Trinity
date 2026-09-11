#include "../src/core/readiness.h"
#include "../src/core/version_mapping.h"
#include "../src/game/crime_hook_contract.h"
#include "../src/game/inventory_hook_contract.h"
#include "../src/game/inventory_logic.h"
#include "../src/game/player_logic.h"
#include "../src/game/equipment_logic.h"
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
        Expect(MoveComponentOwnerOffsetForRevision(2692) == 0x298,
               "pre-2.01 locomotion components must retain move-owner offset 0x298");
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

    void Tu202UsesOnlyTheConfirmedModernInventoryContract()
    {
        using trinity::core::UsesTu201CompatibleRevision;

        Expect(UsesTu201CompatibleRevision(2760),
               "TU 2.01 must retain its modern inventory contract");
        Expect(UsesTu201CompatibleRevision(2850),
               "TU 2.02 must select the verified modern inventory contract");
        Expect(!UsesTu201CompatibleRevision(2692),
               "TU 2.00.02 must not be routed through the newer inventory ABI");
        Expect(!UsesTu201CompatibleRevision(2851),
               "an unverified newer revision must not inherit TU 2.02 ABI selection");
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
        Expect(RealmFlagOffsetForRevision(2850) == 0x1FD,
               "TU 2.02 realm selection must use the live-verified TLS byte at +0x1FD");
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
}

int main()
{
    DelayedCodeIsRetriedUntilReady();
    MissingCodeTimesOutWithoutAnExtraFullSleep();
    PeRevisionMapsToCurrentTitleUpdate();
    CurrentUpdateUsesCompatibleReadinessProfile();
    MovementOwnerOffsetTracksCurrentLayout();
    CurrentUpdateRejectsLegacyFuzzySignatures();
    Tu202UsesOnlyTheConfirmedModernInventoryContract();
    InventoryRootAnchorTracksCurrentInstructionLayout();
    RealmFlagOffsetTracksCurrentTlsLayout();
    TrustScalingUsesTheFirstPositiveGain();
    CachedTrustBaselineWinsOverAliasedLiveRecord();
    AddItemRequiresAnAuthoritativeServerHolder();
    AddItemRetriesOnlyWhileAuthorityIsMissing();
    AuthoritativeHolderCaptureRejectsUnsafeCandidates();
    GodModeRequiresStrictPlayerTarget();
    IdentifiedEquipmentWinsOverPartySlot();
    EquipmentLookupUsesTheOwningCharacter();
    MountClassificationUsesTheTypeDescriptorTag();
    VehiclesDoNotConsumeTheOongkaTrackingSlot();
    EquipmentDemandRefreshesCharactersWithoutStatCheats();
    MissingCatalogResolverIsRetriedAfterTheInterval();
    TrustRecordUsesTheCopiedValueField();
    ExecutableDebugSectionIsScanned();
    if (failures == 0)
        std::puts("readiness tests passed");
    return failures == 0 ? 0 : 1;
}
