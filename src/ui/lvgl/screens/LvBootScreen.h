#pragma once

#include "UIManager.h"

class LvBootScreen : public LvScreen {
public:
    void createUI(lv_obj_t* parent) override;
    const char* title() const override { return "Boot"; }

    void setProgress(float progress, const char* status);
    void showError(const char* message);
    // Borrows a static-lifetime literal and reuses the existing title widget.
    // Does not create widgets or copy/allocate a new label text buffer.
    void showAllocationError(const char* staticMessage);

private:
    lv_obj_t* _lblTitle = nullptr;
    lv_obj_t* _bar = nullptr;
};
