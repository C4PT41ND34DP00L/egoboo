#include "game/module/ObjectHandler.hpp"
#include "game/char.h"
#include "game/profiles/ProfileSystem.hpp"

ObjectHandler::ObjectHandler() :
	_characterList(),
	_characterMap(),
	_loopDepth(0),
	_terminationList()
{
	//ctor
}

bool ObjectHandler::remove(const CHR_REF ichr)
{
    if (!exists(ichr)) {
    	return false;
    }

#if (DEBUG_SCRIPT_LEVEL > 0) && defined(DEBUG_PROFILE) && defined(_DEBUG)
    chr_log_script_time( ichr );
#endif

    // if we are inside a ChrList loop, do not actually change the length of the
    // list. This will cause some problems later.
   
    _terminationList.push_back(ichr);

    // at least mark the object as "waiting to be terminated"
    get(ichr)->terminateRequested = true;

    return true;
}

bool ObjectHandler::exists(const CHR_REF character) const
{
    const auto &result = _characterMap.find(character);

	if(result == _characterMap.end()) {
		return false;
	}

	return !result->second->terminateRequested;
}


CHR_REF ObjectHandler::insert(const PRO_REF profile, const CHR_REF override)
{
	//Make sure the profile is valid
    if(!_profileSystem.isValidProfileID(profile))
    {
        log_warning("ChrList_allocate() - Tried to spawn character with invalid ProfileID: %d\n", profile);
        return INVALID_CHR_REF;
    }

    CHR_REF ichr = INVALID_CHR_REF;

    if(override != INVALID_CHR_REF)
    {
        if(_characterMap.find(override) == _characterMap.end())
        {
            ichr = override;
        }
        else
        {
            log_warning( "ChrList_allocate() - failed to override a character? character %d already spawned? \n", REF_TO_INT( override ) );
        }
    }
    else
    {
        //Find first unused CHR_REF slot
        for(CHR_REF i = 0; i < MAX_CHR; ++i)
        {
            if(_characterMap.find(i) == _characterMap.end())
            {
                ichr = i;
                break;
            }
        }

        //No free slots remaining?
        if(ichr == INVALID_CHR_REF)
        {
            log_warning( "ChrList_allocate() - No free character slots available\n" );
        }
    }

    if (ichr != INVALID_CHR_REF)
    {
        std::shared_ptr<chr_t> object = std::make_shared<chr_t>(profile, ichr);

        if(!object) {
            log_warning( "ChrList_allocate() - Unable to allocate object memory\n" );
            return INVALID_CHR_REF;
        }

        // allocate the new one
        if(_characterMap.emplace(ichr, object).second == false) {
            log_warning( "ChrList_allocate() - Failed character allocation, object already exists\n" );
            return INVALID_CHR_REF;
        }
        _characterList.push_back(object);
    }

    return ichr;
}

void ObjectHandler::cleanup()
{
    if(_loopDepth > 0) {
        return;
    }

    // go through and delete any characters that were
    // supposed to be deleted while the list was iterating
    for(CHR_REF ichr : _terminationList)
    {
        _characterMap.erase(ichr);
    }
    _terminationList.clear();

    _characterList.remove_if(
    	[](const std::shared_ptr<chr_t> &element) 
        {
        	return element->terminateRequested;
        });
}

chr_t* ObjectHandler::operator[] (const PRO_REF index)
{
    const auto &result = _characterMap.find(index);

    if(result == _characterMap.end())
    {
        return nullptr;
    }

    return (*result).second.get();
}

const std::shared_ptr<chr_t>& ObjectHandler::get(const PRO_REF index) const
{
    const auto &result = _characterMap.find(index);

    if(result == _characterMap.end())
    {
    	const static std::shared_ptr<chr_t> NULL_OBJ = nullptr;
        return NULL_OBJ;
    }

    return (*result).second;
}

/*
void ObjectHandler::forEach(std::function<void>() predicate)
{
	_loopDepth++;
	std::for_each(_characterMap.begin(), _characterMap.end(), predicate);
	_loopDepth--;

	cleanup();
}
*/

void ObjectHandler::clear()
{
	_characterMap.clear();
	_characterList.clear();
}
