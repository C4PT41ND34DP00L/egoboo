#pragma once

#include "egolib/game/Core/GameEngine.hpp"
#include "egolib/game/GUI/Label.hpp"
#include "egolib/game/GUI/Button.hpp"

namespace Ego {
namespace GUI {

class OptionsButton : public Button {
public:
    OptionsButton(const std::string &label);

    virtual void draw(DrawingContext& drawingContext) override;

    void setPosition(const Point2f& position) override;

private:
    Label _label;
};

} // namespace GUI
} // namespace Ego
