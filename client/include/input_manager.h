#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

/**
 * @file input_manager.h
 * @brief Input handling system
 */

namespace Web2DEngine {
namespace Client {

/**
 * @brief Manages user input (keyboard, mouse)
 */
class InputManager {
public:
    InputManager();
    ~InputManager();
    
    /**
     * @brief Initialize the input manager
     * @return true if successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Update input state (call once per frame)
     */
    void update();
    
    /**
     * @brief Check if a key is currently pressed
     * @param keyCode Key code to check
     * @return true if pressed, false otherwise
     */
    bool isKeyPressed(int keyCode) const;
    
    /**
     * @brief Get mouse position
     * @param x Output X coordinate
     * @param y Output Y coordinate
     */
    void getMousePosition(int& x, int& y) const;
    
    /**
     * @brief Shutdown the input manager
     */
    void shutdown();
};

} // namespace Client
} // namespace Web2DEngine

#endif // INPUT_MANAGER_H
