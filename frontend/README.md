# Web2D Social Game Engine - Frontend

Browser-based game client using the SimpleGame engine (https://github.com/clansdown/SimpleGame).

## Features

- **SimpleGame Integration**: Uses the SimpleGame engine for sprite-based 2D rendering
- **Real-time Communication**: Connects to the C++ backend via JSON API
- **Social Features**: Built-in chat system for player communication
- **Responsive Design**: Adapts to different screen sizes

## Project Structure

```
frontend/
├── index.html           # Main HTML entry point
├── src/
│   ├── api-client.js    # Backend API communication
│   ├── game.js          # Core game logic with SimpleGame
│   ├── main.js          # Application entry point
│   └── styles.css       # Game styling
└── assets/              # Game assets (images, sounds, etc.)
```

## Getting Started

### Development

1. **Simple HTTP Server**:
   ```bash
   cd frontend
   python3 -m http.server 8000
   ```
   Then open http://localhost:8000 in your browser.

2. **Node.js HTTP Server**:
   ```bash
   cd frontend
   npx http-server -p 8000
   ```

3. **VS Code Live Server**:
   - Install the "Live Server" extension
   - Right-click on `index.html` and select "Open with Live Server"

### Backend Connection

The frontend expects the backend API to be running on `http://localhost:8080`. To change this:

1. Edit `src/main.js`
2. Update the ApiClient initialization:
   ```javascript
   const apiClient = new ApiClient('http://your-backend-url:port');
   ```

## SimpleGame Engine

This project uses the SimpleGame engine from https://github.com/clansdown/SimpleGame.

The engine is loaded via CDN in `index.html`:
```html
<script src="https://cdn.jsdelivr.net/gh/clansdown/SimpleGame@main/simplegame.js"></script>
```

For local development or offline use, you can:
1. Clone the SimpleGame repository
2. Copy the engine file to `frontend/libs/`
3. Update the script tag in `index.html`

## Game Controls

- **WASD** or **Arrow Keys**: Move player
- **Chat**: Type in the chat box and press Enter to send messages

## API Integration

The frontend communicates with the backend using the following endpoints:

### Game State
```javascript
// Get current game state
await apiClient.getGameState();

// Update player position and score
await apiClient.updateGameState({
  player: { x: 100, y: 200 },
  score: 50
});
```

### Player Actions
```javascript
// Send player action to server
await apiClient.sendPlayerAction({
  action: 'key_press',
  key: 'w',
  timestamp: Date.now()
});
```

### Chat Messages
```javascript
// Get messages
await apiClient.getMessages();

// Send a message
await apiClient.sendMessage({
  text: 'Hello!',
  timestamp: Date.now()
});
```

## Customization

### Adding Game Assets

1. Place images in `assets/` folder
2. Load them in `game.js`:
   ```javascript
   const playerSprite = new Image();
   playerSprite.src = 'assets/player.png';
   ```

### Styling

Edit `src/styles.css` to customize:
- Colors and theme
- UI layout
- Chat box appearance
- Canvas borders

### Game Logic

The main game loop is in `src/game.js`:
- `update()`: Game state updates
- `render()`: Drawing to canvas
- `syncWithServer()`: Server synchronization

## Production Deployment

### Static Hosting

Deploy to any static hosting service:
- **GitHub Pages**: Push to `gh-pages` branch
- **Netlify**: Connect your repository
- **Vercel**: Import your project
- **AWS S3**: Upload files to S3 bucket with static hosting

### Build Optimization

For production:
1. Minify JavaScript files
2. Optimize images
3. Enable gzip compression
4. Set up CDN for assets
5. Configure CORS properly

### CORS Configuration

If your backend is on a different domain, ensure CORS headers are set:
```cpp
response.headers["Access-Control-Allow-Origin"] = "https://your-frontend-domain.com";
response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
response.headers["Access-Control-Allow-Headers"] = "Content-Type";
```

## Browser Compatibility

Tested on:
- Chrome 90+
- Firefox 88+
- Safari 14+
- Edge 90+

Requires:
- ES6+ JavaScript support
- Canvas API
- Fetch API

## Troubleshooting

### Backend Connection Issues

If you see CORS errors in the console:
1. Ensure the backend is running
2. Check CORS headers in backend response
3. Use a CORS proxy for development (not recommended for production)

### SimpleGame Not Loading

If SimpleGame fails to load from CDN:
1. Check your internet connection
2. Try loading from a local copy
3. Check browser console for errors

### Performance Issues

To improve performance:
1. Reduce canvas size in CSS
2. Limit the number of rendered objects
3. Optimize the game loop
4. Use requestAnimationFrame properly

## Development Tips

- Use browser DevTools to debug JavaScript
- Monitor Network tab for API calls
- Use Console for logging game state
- Test on different screen sizes

## License

See the LICENSE file in the repository root.
