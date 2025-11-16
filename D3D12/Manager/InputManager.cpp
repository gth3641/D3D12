#include "InputManager.h"
#include "DirectXManager.h"
#include "Support/WinInclude.h"

extern const int OBJ_RES_NUM;

bool InputManager::Init()
{
	float m_MoveSpeed = 0.f;
	 
	m_ToggleDelegate.reserve(KEY_MAX);
	for (int i = 0; i < KEY_MAX; i++)
	{
		setKeyUp(i, false);
		setKeyDown(i, false);

		m_keyToggle[i] = isToggleKey(i);

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
		if (isToggleKey(i) != m_keyToggle[i])
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

			m_keyToggle[i] = isToggleKey(i);
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

void InputManager::InitWalk(int i)
{
	m_CamWalks.clear();

	m_MoveSpeed =
		OBJ_RES_NUM == RES_SPONZA ? 5.0f :
		OBJ_RES_NUM == RES_SAN_MIGUEL ? 0.5f :
		OBJ_RES_NUM == RES_GALLERY ? 0.1f :
		OBJ_RES_NUM == RES_ISCV2 ? 0.3f : 0.f;

	if (i == RES_SPONZA)
	{
		CamWalkInit_sponzaA();
	}
	else if (i == RES_SAN_MIGUEL)
	{
		CamWalkInit_San_Miguel();
	}
	else if (i == RES_GALLERY)
	{
		CamWalkInit_gallery();
	}
	else if (i == RES_ISCV2)
	{
		CamWalkInit_ISCV2();
	}

}


void InputManager::CamWalkInit_sponzaA()
{
	const double pi = 3.14159265358979;
	float radian = (float)pi / 180.0f;
	float rotPerSec = radian * 90.0f / 24;

	m_CamWalks =
	{
		{ {0, m_MoveSpeed, 0}, {(-90) * radian,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //120

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //240
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //300
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//310
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, //360

		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //370
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //380
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //390
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //400
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //480
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
	};
}


void InputManager::CamWalkInit_San_Miguel()
{
	const double pi = 3.14159265358979;
	float radian = (float)pi / 180.0f;
	float rotPerSec = radian * 90.0f / 24;

	m_CamWalks =
	{
		{ {0, 0, 0}, {(90) * radian,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//10
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//20
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//30
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//40
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//50

		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },//60
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },//70

		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {0,0} },

		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//80
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//90
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },

		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {0,0} },//100

		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//110
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//120
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//130

		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },//140
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },//150
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//160
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//180
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },

		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} },
		{ {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {-rotPerSec,0} }, { {0, 0, 0}, {0,0} },

		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },
		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },

	};
}

void InputManager::CamWalkInit_ISCV2()
{
	const double pi = 3.14159265358979;
	float radian = (float)pi / 180.0f;
	float rotPerSec = radian * 90.0f / 24;

	m_CamWalks =
	{
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//110
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//120
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },

		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//110
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//120
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },

		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//110
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//120
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },

		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//110
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//120
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },

		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//110
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },//120
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },

		{ {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //120
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170

		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {0,0} },

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //240
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //300
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//310
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} },
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, //360

		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //370
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //380
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //390
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //400
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //480
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
	};
}

void InputManager::CamWalkInit_gallery()
{
	const double pi = 3.14159265358979;
	float radian = (float)pi / 180.0f;
	float rotPerSec = radian * 90.0f / 24;

	m_CamWalks =
	{
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //120

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //170
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} },
		{ {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {-rotPerSec,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //240

		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //300
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },//310
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, 
		{ {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {rotPerSec,0} }, { {0, 0, 0}, {0,0} }, { {0, 0, 0}, {0,0} }, //360

		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //370
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //380
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, //390
		{ {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} }, { {m_MoveSpeed, 0, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //400
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, //480
		{ {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} }, { {0, m_MoveSpeed, 0}, {0,0} },
	};


}