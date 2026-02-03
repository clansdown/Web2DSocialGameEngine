#ifndef RENDERER_H
#define RENDERER_H

/**
 * @file renderer.h
 * @brief 2D sprite-based rendering system
 */

namespace Web2DEngine {
namespace Client {

/**
 * @brief 2D Renderer for sprite-based games
 */
class Renderer {
public:
    Renderer();
    ~Renderer();
    
    /**
     * @brief Initialize the renderer
     * @return true if successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Begin a new frame
     */
    void beginFrame();
    
    /**
     * @brief End the current frame and present to screen
     */
    void endFrame();
    
    /**
     * @brief Shutdown the renderer
     */
    void shutdown();
};

} // namespace Client
} // namespace Web2DEngine

#endif // RENDERER_H
