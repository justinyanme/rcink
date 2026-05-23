#pragma once
#ifdef ARDUINO
namespace rcpaper {
// Render-side warning bag. Any painter that has to clip, elide, or otherwise
// degrade its output should call `render_warn(...)`. `render_page` checks the
// flag at the end of each paint and overlays a small red "!" badge in the
// bottom-right so the operator knows to inspect the serial log for `[warn]`
// lines, without us having to spam an always-on indicator.
void render_warn(const char* msg);
void render_warn_clear();
bool render_has_warnings();
}  // namespace rcpaper
#endif
