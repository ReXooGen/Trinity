#pragma once

#include <cstddef>
#include <cstdint>

namespace trinity::ui
{
    // Reusable menu rows. Every widget draws one 36px row between Begin() and
    // End() (framework.h) and returns true when it was activated / changed
    // this frame. All are navigable by keyboard, mouse and controller, and
    // each reports its control scheme to the footer hint bar automatically.

    // A plain action row (Enter / A / click fires it).
    bool Option(const char* label, const char* desc = nullptr);

    // Option with the game's own icon at the left, lazily loaded from `icon` -
    // the sprite name the game itself stores for the item or category (see
    // icons.h). The icon box is reserved even when there is no icon, keeping
    // lists aligned.
    bool OptionItem(const char* label, const char* icon,
                    const char* desc = nullptr);

    // On/off switch. Enter/A/click or Left/Right flips it; the knob animates.
    bool Toggle(const char* label, bool* value, const char* desc = nullptr);

    // Numeric rows. Left/Right steps (held: x10). Enter starts inline typing
    // of an exact value (keyboard; Enter commits, moving away cancels).
    // Del / X resets to `defV`.
    bool FloatOption(const char* label, float* value, float minV, float maxV,
                     float step, float defV, const char* fmt = "%.2f",
                     const char* desc = nullptr);

    // A single row that fuses a Toggle and a numeric slider - the on/off
    // switch and its multiplier share one line (e.g. "Super Run" + run speed).
    // Enter / A / click flips `enabled`; Left/Right steps `value` (held: x10);
    // Del / X resets it to `defV`. Returns true when either changed.
    bool ToggleFloat(const char* label, bool* enabled, float* value,
                     float minV, float maxV, float step, float defV,
                     const char* fmt = "%.2f", const char* desc = nullptr);
    bool IntOption(const char* label, int* value, int minV, int maxV,
                   int step, int defV, const char* desc = nullptr);

    // A number and the action that applies it, on ONE row (e.g. "Set All").
    // Left/Right step the amount (held: x10) and Del / X resets it to `defV`,
    // exactly like IntOption - what differs is Enter, and only Enter.
    //
    // A row like this wants Enter to mean two things at once: type an exact
    // amount (a value row's Enter) and run the action (an action row's Enter).
    // The way out is that they are the same press at different moments - Enter
    // starts typing, and the Enter that COMMITS the number is the one that
    // fires. So the amount is always stated at the moment it is applied, and no
    // press both starts an edit and writes to forty items. The current amount
    // is the placeholder, so:
    //   * Enter, Enter          - fire with the amount already on the row;
    //   * Enter, 250, Enter     - fire with 250;
    //   * Enter, Esc / move off - fire with nothing.
    // A pad has nothing to type with, so there A just fires with the amount on
    // the row (the same split ItemRow makes).
    //
    // Returns true on the frame it fires, with *value holding the amount to
    // apply. It does NOT return true when the amount merely changed - stepping
    // an amount is not asking for it to be applied.
    bool IntAction(const char* label, int* value, int minV, int maxV,
                   int step, int defV, const char* desc = nullptr);

    // Same as ToggleFloat, but for a whole-number value ("Max Stack Size"
    // 999999, "Slot Size" 999) - no decimal formatting.
    bool ToggleInt(const char* label, bool* enabled, int* value,
                   int minV, int maxV, int step, int defV,
                   const char* desc = nullptr);

    // Cycles through `items`; Left/Right picks, Enter/A advances.
    bool Combo(const char* label, int* index, const char* const* items,
               int count, const char* desc = nullptr);

    // Pushes submenu `id` when activated; `label` (minus any trailing
    // "  (count)") becomes its breadcrumb title.
    bool Submenu(const char* label, const char* id, const char* desc = nullptr);

    // Submenu with the game's own icon at the left (see OptionItem / icons.h).
    bool SubmenuItem(const char* label, const char* icon, const char* id,
                     const char* desc = nullptr);

    // Type-to-filter row for long lists. Activating it starts text capture
    // (all keyboard input is swallowed from the game while typing - see
    // State::textCapture); Enter / Esc or moving off the row stops it.
    // Backspace edits, Del / X clears the whole filter. Returns true when
    // the buffer changed this frame.
    bool Search(char* buf, size_t cap, const char* desc = nullptr);

    // Editable text field (e.g. rename bookmark). Enter / A starts typing;
    // Enter / Esc stops. Backspace edits, Del / X clears. Returns true when buffer changed.
    bool TextInput(const char* label, char* buf, size_t cap, const char* desc = nullptr);

    // Saved Location bookmark row: Enter/A opens options, Del/X deletes location directly.
    enum class BookmarkAction { None, Open, Delete };
    BookmarkAction BookmarkRow(const char* label, const char* desc = nullptr);

    // --- Inventory item row ---------------------------------------------------
    // One item with its quantity edited IN PLACE - browsing and editing are the
    // same list, so nothing pops up over the page. Left/Right step the amount
    // (held: x10), Enter types an exact one, Del / X removes the item. When
    // `locked` is true the amount still shows but nothing edits it.
    //
    // Removal is confirmed by a SECOND Del / X, so the first press only arms
    // the row and says so; arming lapses on its own after a couple of seconds.
    // (Removal used to be strictly one-way, which is why it is guarded at all.
    // Inventory -> Add Item can put an item back now, but only if you know what
    // it was - so the confirmation stays.)
    //
    // `key` identifies the item across frames. The value rows key their
    // in-progress typing on the value's own address, but the inventory snapshot
    // is rebuilt every ~120ms, so item rows have no such stable pointer. Build
    // `key` from whatever addresses the item (storage, category, position,
    // type) and it does better than a pointer would: if the row's item changes
    // under an edit, the key changes with it and the edit is dropped instead of
    // landing on whatever moved in.
    //
    // Returns what the user did; *outQty receives the new ABSOLUTE quantity on
    // SetQty (already clamped), so the caller just stores it.
    enum class ItemEdit { None, SetQty, Remove };
    ItemEdit ItemRow(const char* label, const char* icon, long long qty,
                     unsigned long long key, bool locked, long long* outQty,
                     const char* desc = nullptr);

    // --- Color swatch row -------------------------------------------------------
    // A row of up to 12 circular color buttons (the dye editor's preset
    // palette). Left/Right move the focus ring along the row and the mouse
    // takes it wherever it points; Enter / A / click picks the focused
    // swatch. `rgb` packs one color per swatch as 0xRRGGBB.
    //
    // `cursor` is the row's remembered focus column (clamped into range) -
    // give each row its own int so the ring stays put across frames.
    // `current`, when 0..count-1, draws a dot on that swatch: "this is the
    // color the zone wears right now".
    //
    // With a non-empty `label` the swatches sit right-aligned like any value
    // column (a one-swatch row makes a labelled "apply this color" button);
    // with an empty label they start at the left inset, so consecutive rows
    // stack into a grid.
    //
    // `clearIndex`, when 0..count-1, draws that swatch as a "remove / none"
    // marker (a slashed disc) instead of a color - its `rgb` entry is ignored.
    // The caller still gets that column back when it is picked, and decides to
    // clear rather than apply.
    //
    // Returns the picked column on the frame it fires, else -1.
    int SwatchRow(const char* label, const uint32_t* rgb, int count,
                  int* cursor, int current = -1, const char* desc = nullptr,
                  int clearIndex = -1);

    // --- Catalog "add this item" row ------------------------------------------
    // ItemRow's sibling for Inventory -> Add Item: same look, same icon + name +
    // amount layout, same Left/Right stepping (held: x10). What differs is what
    // the row is FOR - an item you do not own yet:
    //   * the amount is how many to ADD, not a quantity to overwrite, so it
    //     starts at 1 rather than being read from anything;
    //   * Enter / A ADDS. It does not start inline typing the way ItemRow does,
    //     because here the amount is the input and adding is the action;
    //   * there is nothing to remove.
    // For a large amount, add one and set the exact quantity in the Editor -
    // that is what the quantity editor is for, and it beats holding Right.
    //
    // The amount lives in the widget, keyed by `key`, and resets to 1 when you
    // move to another row: each row is its own "add this many of this", and a
    // count left over from the last item is a trap, not a convenience.
    //
    // Returns how many to add on the frame the row was activated, otherwise 0.
    // `locked` draws the row inert (e.g. before the save has finished loading).
    long long ItemAddRow(const char* label, const char* icon,
                         unsigned long long key, bool locked,
                         const char* desc = nullptr);

    // --- Keybind rows ---------------------------------------------------------
    // One action bound to a keyboard key AND a controller button, shown on a
    // single row as two side-by-side columns so the two devices read as
    // separate, paired choices rather than a flat list of "<action> Key" /
    // "<action> Button" rows.
    //
    // Left/Right move the focus between the Keyboard and Controller column;
    // Enter / A / click asks to rebind the focused column; Del / X asks to
    // reset it. `cursor` is the caller-owned focus column (0 = keyboard,
    // 1 = controller) so the highlight stays put across frames - give each row
    // its own int. `keyText` / `padText` are the current bind displays the
    // caller formats (pass "press a key..." / "press a button..." yourself for
    // whichever column is mid-capture, named by `capturingCol`: 0, 1, or -1 for
    // none, which also pulses that column so it reads as "listening").
    //
    // Returns what the user asked for this frame; the caller owns what "rebind"
    // and "reset" actually do (start a capture, restore a default...).
    enum class BindEdit { None, RebindKey, RebindPad, ResetKey, ResetPad };
    BindEdit BindRow(const char* label, int* cursor, const char* keyText,
                     const char* padText, int capturingCol,
                     const char* desc = nullptr);

    // Draws the "Keyboard" / "Controller" column titles above a run of BindRows,
    // aligned to their two columns. Not a navigable row - it only labels the
    // columns once at the top of the list. Call it right after Begin(), before
    // the first BindRow.
    void BindHeader();
}
