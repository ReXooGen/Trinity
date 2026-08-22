#pragma once

namespace trinity::gui
{
    // Called every frame between ImGui::NewFrame() and ImGui::Render().
    void Render();

    // Whether the overlay has anything to draw this frame (menu open or an
    // extra like the FPS counter enabled). Gates the whole render path.
    bool WantsDraw();
}
