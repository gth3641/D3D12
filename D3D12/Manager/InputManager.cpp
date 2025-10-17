#include "InputManager.h"
#include "Support/WinInclude.h"

bool InputManager::Init()
{
	m_ToggleDelegate.reserve(KEY_MAX);
	for (int i = 0; i < KEY_MAX; i++)
	{
		setKeyUp(i, false);
		setKeyDown(i, false);

		_keyToggle[i] = isToggleKey(i);

		Delegate toggleDel;
		m_ToggleDelegate.push_back(toggleDel);
	}

	return true;
}

void InputManager::Shutdown()
{

}

void InputManager::update(float deltaTime)
{
	for (int i = 0; i < KEY_MAX; i++)
	{
		if (isToggleKey(i) != _keyToggle[i])
		{
			for (auto iter = m_ToggleDelegate[i].delegateMap.begin(); iter != m_ToggleDelegate[i].delegateMap.end(); ++iter)
			{
				if (iter->first == nullptr)
				{
					continue;
				}

				size_t size = iter->second.size();
				for (size_t j = 0; j < size; ++j)
				{
					iter->second[j]();
				}
			}

			_keyToggle[i] = isToggleKey(i);
		}
	}
}


bool InputManager::isOneKeyDown(int key)
{
	if (GetAsyncKeyState(key) & 0x8000)
	{
		if (!this->getKeyDown()[key])
		{
			this->setKeyDown(key, true);
			return true;
		}
	}
	else
	{
		this->setKeyDown(key, false);
	}
	return false;
}

bool InputManager::isOneKeyUp(int key)
{
	if (GetAsyncKeyState(key) & 0x8000)
	{
		this->setKeyUp(key, true);
	}
	else
	{
		if (this->getKeyUp()[key])
		{
			this->setKeyUp(key, false);
			return true;
		}
	}
	return false;
}

bool InputManager::isStateKeyDown(int key)
{
	if (GetAsyncKeyState(key) & 0x8000)
	{
		return true;
	}

	return false;
}

bool InputManager::isToggleKey(int key)
{
	if (GetKeyState(key) & 0x0001)
	{
		return true;
	}

	return false;
}

