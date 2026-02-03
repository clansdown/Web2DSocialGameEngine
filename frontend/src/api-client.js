/**
 * API Client for communicating with the C++ backend
 */
class ApiClient {
    constructor(baseUrl = 'http://localhost:8080') {
        this.baseUrl = baseUrl;
    }

    /**
     * Send a POST request to the API
     */
    async post(endpoint, data) {
        try {
            const response = await fetch(`${this.baseUrl}${endpoint}`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(data)
            });
            return await response.json();
        } catch (error) {
            console.error('API Error:', error);
            return { status: 'error', message: error.message };
        }
    }

    /**
     * Send a GET request to the API
     */
    async get(endpoint) {
        try {
            const response = await fetch(`${this.baseUrl}${endpoint}`, {
                method: 'GET',
                headers: {
                    'Content-Type': 'application/json',
                }
            });
            return await response.json();
        } catch (error) {
            console.error('API Error:', error);
            return { status: 'error', message: error.message };
        }
    }

    /**
     * Get current game state
     */
    async getGameState() {
        return await this.get('/api/game_state');
    }

    /**
     * Update game state
     */
    async updateGameState(state) {
        return await this.post('/api/game_state', state);
    }

    /**
     * Send a player action
     */
    async sendPlayerAction(action) {
        return await this.post('/api/player_action', action);
    }

    /**
     * Get messages
     */
    async getMessages() {
        return await this.get('/api/messages');
    }

    /**
     * Send a message
     */
    async sendMessage(message) {
        return await this.post('/api/messages', message);
    }
}
