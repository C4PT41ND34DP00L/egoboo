#include "egolib/game/GUI/DrawingContext.hpp"
#include "egolib/game/GUI/UIManager.hpp"

namespace Ego {
namespace GUI {

DrawingContext::DrawingContext()
    : DrawingContext(false)
{}

DrawingContext::DrawingContext(bool useDerived)
    : useDerived(useDerived)
{}

DrawingContext::~DrawingContext()
{}

} // namespace GUI
} // namespace Ego
