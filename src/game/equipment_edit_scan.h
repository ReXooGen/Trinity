#pragma once
#include "inventory_scan.h"

namespace trinity::game
{
    // Completion/match evidence belongs to this exact holder generation. Never
    // retain it when Bind rejects a mutation or a bucket allocation changes.
    struct EquipmentEditScan
    {
        InventoryScan cursor{};
        bool complete = false;
        unsigned matches = 0;
        unsigned restarts = 0;
        uint64_t nextAttempt = 0;

        void Reset()
        {
            cursor = {};
            complete = false;
            matches = 0;
            ++restarts;
        }

        bool Bind(uintptr_t holder, uintptr_t stride, uint64_t epoch)
        {
            if (cursor.Bind(holder, stride, epoch)) return true;
            Reset();
            // Rebind fresh in this visit, rather than wasting every second
            // visit after churn. This never authorizes an old slot pointer.
            return cursor.Bind(holder, stride, epoch);
        }

        void Finish(InventoryScan::Result result)
        {
            if (result == InventoryScan::Result::Invalid) Reset();
            else complete = result == InventoryScan::Result::Complete;
        }
    };
}
