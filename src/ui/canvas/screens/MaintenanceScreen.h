#pragma once

#include "Screen.h"
#include "Theme.h"

// Fixed presentation for the owner-loop maintenance adapter. It can request a
// new forced restart only after the failure warning is visible.
class MaintenanceScreen : public Screen {
public:
    const char* title() const override { return "Maintenance"; }
    void show(const char* detail, bool failed = false) {
        _detail = detail; _failed = failed; _restart = false; _warningRendered = false;
    }
    bool takeRestart() { const bool value = _restart; _restart = false; return value; }
    bool handleKey(const KeyEvent& event) override {
        if (_failed && _warningRendered && !event.repeat && !event.ctrl && !event.alt && !event.fn &&
            (event.character == 'r' || event.character == 'R')) _restart = true;
        return true;
    }
    void render(M5Canvas& canvas) override {
        Theme::useSmallFont(canvas);
        canvas.setTextColor(_failed ? Theme::ERROR : Theme::PRIMARY);
        canvas.setCursor(8, Theme::CONTENT_Y + 8);
        canvas.print(_failed ? "Maintenance stopped" : "Finishing pending work");
        canvas.setTextColor(Theme::SECONDARY);
        canvas.setCursor(8, Theme::CONTENT_Y + 28); canvas.print(_detail);
        if (_failed) {
            canvas.setCursor(8, Theme::CONTENT_Y + 46); canvas.print("Pending data may be lost");
            canvas.setCursor(8, Theme::CONTENT_Y + 60); canvas.print("if you force a restart.");
            canvas.setTextColor(Theme::PRIMARY);
            canvas.setCursor(8, Theme::CONTENT_Y + 82); canvas.print("R: Force restart");
            _warningRendered = true;
        } else {
            canvas.setCursor(8, Theme::CONTENT_Y + 50); canvas.print("Keep the device powered on.");
            canvas.setCursor(8, Theme::CONTENT_Y + 68); canvas.print("It will restart when ready.");
        }
    }
private:
    const char* _detail = "Saving and closing connections";
    bool _failed = false, _restart = false;
    bool _warningRendered = false;
};
