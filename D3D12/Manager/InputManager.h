#pragma once

#include "Util/Util.h"
#include <bitset>

#define DX_INPUT InputManager::Get()
#define KEY_MAX 256

struct CamMove
{
    CamMove(float x, float y, float z) : x(x), y(y), z(z)
    {
    }

    float x = 0.0f;
    float y = 0.0f;
	float z = 0.0f;
};

struct CamRotate
{
    CamRotate(float x, float y)
        : x(x), y(y)
    {
	}

    float x = 0.0f;
    float y = 0.0f;
};


struct CamWalk
{
    CamWalk(CamMove InCamMove, CamRotate InCamRotates)
		: CamMove(InCamMove), CamRotate(InCamRotates) 
    {
	}

    CamMove CamMove;
    CamRotate CamRotate;
};


class InputManager
{
public: // Singleton pattern to ensure only one instance exists 
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    inline static InputManager& Get()
    {
        static InputManager instance;
        return instance;
    }

public:
    InputManager() = default;

public: // Functions
    bool Init();
    void Shutdown();
    void update(float deltaTime);

    bool isOneKeyDown(int key);
    bool isOneKeyUp(int key);
    bool isStateKeyDown(int key);
    bool isToggleKey(int key);

    std::bitset<KEY_MAX> getKeyUp(void) { return m_keyUp; }
    std::bitset<KEY_MAX> getKeyDown(void) { return m_keyDown; }

    void setKeyUp(int key, bool state) { m_keyUp.set(key, state); }
    void setKeyDown(int key, bool state) { m_keyDown.set(key, state); }

    void SetMouseLock();

    template<class T, class P>
    void AddDelegate(int vertualKey,T* object, P pred)
    {
        if (vertualKey >= KEY_MAX || vertualKey < 0)
        {
            return;
        }

        if (object == nullptr)
        {
            return;
        }

        m_ToggleDelegate[vertualKey].AddDelegate(object, pred);
    }

    template<class T>
    void AddDelegate(int vertualKey, T* object, void (T::* method)())
    {
        if (vertualKey >= KEY_MAX || vertualKey < 0)
        {
            return;
        }

        if (object == nullptr)
        {
            return;
        }

        m_ToggleDelegate[vertualKey].AddDelegate(object, method);
    }

    void RemoveDelegate(int vertualKey, const void* object)
    {
        if (vertualKey >= KEY_MAX || vertualKey < 0)
        {
            return;
        }

        if (object == nullptr)
        {
            return;
        }

        m_ToggleDelegate[vertualKey].RemoveDelegate(object);
    }

	const CamWalk& GetCamWalk(size_t index) const { return m_CamWalks[index]; }

    void InitWalk(int i);

private:

    void CamWalkInit_gallery();
    void CamWalkInit_sponzaA();
    void CamWalkInit_San_Miguel();
    void CamWalkInit_ISCV2();

private:
    std::bitset<KEY_MAX> m_keyUp;
    std::bitset<KEY_MAX> m_keyDown;
    std::bitset<KEY_MAX> m_keyToggle;

    bool m_isMouseLock = false;

    std::vector<Delegate> m_ToggleDelegate;

	std::vector<CamWalk> m_CamWalks;

    float m_MoveSpeed = 0.f;

};

