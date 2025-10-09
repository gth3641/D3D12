#pragma once

#include "Util/Util.h"
#include <bitset>

#define DX_INPUT InputManager::Get()
#define KEY_MAX 256





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

    std::bitset<KEY_MAX> getKeyUp(void) { return _keyUp; }
    std::bitset<KEY_MAX> getKeyDown(void) { return _keyDown; }

    void setKeyUp(int key, bool state) { _keyUp.set(key, state); }
    void setKeyDown(int key, bool state) { _keyDown.set(key, state); }

    void SetMouseLock();


    //=================Getter=======================//
    //==============================================//

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


private:
    std::bitset<KEY_MAX> _keyUp;
    std::bitset<KEY_MAX> _keyDown;
    std::bitset<KEY_MAX> _keyToggle;

    bool isMouseLock = false;

    std::vector<Delegate> m_ToggleDelegate;

};

