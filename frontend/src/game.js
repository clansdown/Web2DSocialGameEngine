/**
 * Main game logic using SimpleGame engine
 */
class Game {
    constructor(canvas, apiClient) {
        this.canvas = canvas;
        this.apiClient = apiClient;
        this.score = 0;
        
        // Initialize SimpleGame (if available)
        if (typeof SimpleGame !== 'undefined') {
            this.engine = new SimpleGame(canvas);
            this.initSimpleGame();
        } else {
            console.warn('SimpleGame not loaded, using fallback rendering');
            this.initFallback();
        }
        
        this.players = new Map();
        this.lastUpdate = Date.now();
    }

    initSimpleGame() {
        // Configure SimpleGame engine
        console.log('Initializing SimpleGame engine...');
        
        // Create player sprite
        this.player = {
            x: 400,
            y: 300,
            width: 32,
            height: 32,
            color: '#e94560',
            speed: 5
        };
        
        // Set up input handling
        this.keys = {};
        this.setupInput();
    }

    initFallback() {
        // Fallback for when SimpleGame is not available
        this.ctx = this.canvas.getContext('2d');
        this.canvas.width = 800;
        this.canvas.height = 600;
        
        this.player = {
            x: 400,
            y: 300,
            width: 32,
            height: 32,
            color: '#e94560',
            speed: 5
        };
        
        this.setupInput();
    }

    setupInput() {
        window.addEventListener('keydown', (e) => {
            this.keys[e.key] = true;
            this.handlePlayerAction(e.key, true);
        });

        window.addEventListener('keyup', (e) => {
            this.keys[e.key] = false;
            this.handlePlayerAction(e.key, false);
        });
    }

    handlePlayerAction(key, pressed) {
        if (pressed) {
            // Send action to server
            this.apiClient.sendPlayerAction({
                action: 'key_press',
                key: key,
                timestamp: Date.now()
            }).catch(err => console.error('Failed to send action:', err));
        }
    }

    update(deltaTime) {
        // Update player position based on input
        if (this.keys['ArrowUp'] || this.keys['w']) {
            this.player.y -= this.player.speed;
        }
        if (this.keys['ArrowDown'] || this.keys['s']) {
            this.player.y += this.player.speed;
        }
        if (this.keys['ArrowLeft'] || this.keys['a']) {
            this.player.x -= this.player.speed;
        }
        if (this.keys['ArrowRight'] || this.keys['d']) {
            this.player.x += this.player.speed;
        }

        // Keep player in bounds
        this.player.x = Math.max(0, Math.min(this.canvas.width - this.player.width, this.player.x));
        this.player.y = Math.max(0, Math.min(this.canvas.height - this.player.height, this.player.y));

        // Periodically sync with server
        const now = Date.now();
        if (now - this.lastUpdate > 1000) {
            this.syncWithServer();
            this.lastUpdate = now;
        }
    }

    render() {
        if (this.engine) {
            // Use SimpleGame rendering
            this.renderWithSimpleGame();
        } else {
            // Use fallback rendering
            this.renderFallback();
        }
    }

    renderWithSimpleGame() {
        // Clear and render using SimpleGame
        // This is a placeholder - actual SimpleGame API would be used here
        this.renderFallback();
    }

    renderFallback() {
        // Clear canvas
        this.ctx.fillStyle = '#16213e';
        this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);

        // Draw player
        this.ctx.fillStyle = this.player.color;
        this.ctx.fillRect(this.player.x, this.player.y, this.player.width, this.player.height);

        // Draw other players
        this.players.forEach((player, id) => {
            this.ctx.fillStyle = '#4ecca3';
            this.ctx.fillRect(player.x, player.y, 32, 32);
        });
    }

    async syncWithServer() {
        try {
            // Send current state to server
            await this.apiClient.updateGameState({
                player: {
                    x: this.player.x,
                    y: this.player.y
                },
                score: this.score
            });

            // Get game state from server
            const state = await this.apiClient.getGameState();
            if (state.status === 'ok' && state.game_state) {
                // Update other players
                if (state.game_state.players) {
                    this.players.clear();
                    state.game_state.players.forEach(p => {
                        this.players.set(p.id, p);
                    });
                }
            }
        } catch (error) {
            console.error('Sync error:', error);
        }
    }

    addScore(points) {
        this.score += points;
        document.getElementById('score').textContent = this.score;
    }
}
