//********************************************************************************************
//*
//*    This file is part of Egoboo.
//*
//*    Egoboo is free software: you can redistribute it and/or modify it
//*    under the terms of the GNU General Public License as published by
//*    the Free Software Foundation, either version 3 of the License, or
//*    (at your option) any later version.
//*
//*    Egoboo is distributed in the hope that it will be useful, but
//*    WITHOUT ANY WARRANTY; without even the implied warranty of
//*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//*    General Public License for more details.
//*
//*    You should have received a copy of the GNU General Public License
//*    along with Egoboo.  If not, see <http://www.gnu.org/licenses/>.
//*
//********************************************************************************************

/// @file egolib/game/GameStates/SelectCharacterState.cpp
/// @details Select which character a given player is playing
/// @author Johan Jansen

#include "egolib/game/GameStates/SelectCharacterState.hpp"
#include "egolib/game/GameStates/SelectModuleState.hpp"
#include "egolib/game/GameStates/LoadPlayerElement.hpp"
#include "egolib/game/Core/GameEngine.hpp"
#include "egolib/game/GUI/Button.hpp"
#include "egolib/game/GUI/IconButton.hpp"
#include "egolib/game/GUI/Image.hpp"
#include "egolib/game/GUI/Label.hpp"
#include "egolib/game/GUI/ScrollableList.hpp"

SelectCharacterState::SelectCharacterState(std::shared_ptr<LoadPlayerElement> &selectedCharacter)
{
    const int SCREEN_WIDTH = _gameEngine->getUIManager()->getScreenWidth();
    const int SCREEN_HEIGHT = _gameEngine->getUIManager()->getScreenHeight();

	//Load background
	auto background = std::make_shared<Ego::GUI::Image>("mp_data/menu/menu_selectplayers");
    background->setSize(Vector2f(std::min(SCREEN_WIDTH, background->getTextureWidth()), std::min(SCREEN_HEIGHT, background->getTextureHeight())));
	background->setPosition(Point2f(SCREEN_WIDTH, SCREEN_HEIGHT) + Vector2f(-background->getWidth(), -background->getHeight()));
	addComponent(background);

	//Add the buttons
	int yOffset = SCREEN_HEIGHT-80;
	auto backButton = std::make_shared<Ego::GUI::Button>("No character", SDLK_ESCAPE);
	backButton->setPosition(Point2f(20, yOffset));
	backButton->setSize(Vector2f(200, 30));
	backButton->setOnClickFunction(
	[this, &selectedCharacter]() mutable {
		//Unselect any previous selection first
		if(selectedCharacter != nullptr) {
			selectedCharacter->setSelected(false);
		}

		//No character was selected and return to previous state
		selectedCharacter = nullptr;
		endState();
	});
	addComponent(backButton);

	yOffset -= backButton->getHeight() + 10;

	auto selectButton = std::make_shared<Ego::GUI::Button>("Select Character", SDLK_RETURN);
	selectButton->setPosition(Point2f(20, yOffset));
	selectButton->setSize(Vector2f(200, 30));
	selectButton->setOnClickFunction(
	[this] {
		//Accept our selection and return to previous state
		endState();
	});
	selectButton->setVisible(false);
	addComponent(selectButton);

	//Tell them what this screen is all about
	auto infoText = std::make_shared<Ego::GUI::Label>("Select your character\nUse the mouse wheel to scroll.");
	infoText->setPosition(Point2f(150, SCREEN_HEIGHT - 50));
	addComponent(infoText);

	//Players Label
	auto playersLabel = std::make_shared<Ego::GUI::Label>("CHARACTERS");
	playersLabel->setPosition(Point2f(20, 20));
	addComponent(playersLabel);

	auto scrollableList = std::make_shared<Ego::GUI::ScrollableList>();
	scrollableList->setSize(Vector2f(300, _gameEngine->getUIManager()->getScreenHeight() - playersLabel->getY() - 150));
	scrollableList->setPosition(playersLabel->getPosition() + Vector2f(20, playersLabel->getHeight() + 20));

	//Make a button for each loadable character
    for (const std::shared_ptr<LoadPlayerElement> &character : ProfileSystem::get().getSavedPlayers())
	{
		auto characterButton = std::make_shared<Ego::GUI::IconButton>(character->getName(), character->getIcon());
		characterButton->setSize(Vector2f(200, 40));

		//Check if this already has been selected by another player
		if(character->isSelected() && character != selectedCharacter) {
			characterButton->setEnabled(false);
		}

		else {
			characterButton->setOnClickFunction(
				[character, &selectedCharacter, selectButton]() mutable {
					//Unselect any previous selection first
					if(selectedCharacter != nullptr) {
						selectedCharacter->setSelected(false);
					}

					//This is the new character selected by this player
					selectedCharacter = character;
					selectedCharacter->setSelected(true);

					//Show the button that lets them accept their selection now
					selectButton->setVisible(true);
				});
		}

		scrollableList->addComponent(characterButton);
	}
	
	scrollableList->forceUpdate();
	addComponent(scrollableList);
}

void SelectCharacterState::update()
{
}

void SelectCharacterState::drawContainer(Ego::GUI::DrawingContext& drawingContext)
{

}

void SelectCharacterState::beginState()
{
    // menu settings
    Ego::GraphicsSystem::get().window->setGrabEnabled(false);
}
