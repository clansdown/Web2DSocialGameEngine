/**
 * Main entry point for the game
 */

// Initialize API client
const apiClient = new ApiClient();

// Initialize game when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    const canvas = document.getElementById('game-canvas');
    const game = new Game(canvas, apiClient);

    // Chat functionality
    const chatInput = document.getElementById('chat-input');
    const messagesDiv = document.getElementById('messages');

    chatInput.addEventListener('keypress', async (e) => {
        if (e.key === 'Enter' && chatInput.value.trim()) {
            const message = chatInput.value.trim();
            
            // Send message to server
            const result = await apiClient.sendMessage({
                text: message,
                timestamp: Date.now()
            });

            if (result.status === 'ok') {
                // Add message to chat
                addChatMessage('You', message);
                chatInput.value = '';
            }
        }
    });

    function addChatMessage(sender, text) {
        const messageEl = document.createElement('div');
        messageEl.className = 'message';
        messageEl.innerHTML = `<strong>${sender}:</strong> ${text}`;
        messagesDiv.appendChild(messageEl);
        messagesDiv.scrollTop = messagesDiv.scrollHeight;
    }

    // Poll for new messages
    let lastMessageId = 0;
    
    async function pollMessages() {
        try {
            const result = await apiClient.getMessages();
            if (result.status === 'ok' && result.messages) {
                result.messages.forEach(msg => {
                    // Only add new messages
                    if (msg.id && msg.id > lastMessageId) {
                        if (msg.sender && msg.text) {
                            addChatMessage(msg.sender, msg.text);
                        }
                        lastMessageId = msg.id;
                    }
                });
            }
        } catch (error) {
            console.error('Failed to poll messages:', error);
        }
    }

    // Poll messages every 2 seconds
    setInterval(pollMessages, 2000);

    // Game loop
    let lastTime = Date.now();
    
    function gameLoop() {
        const now = Date.now();
        const deltaTime = now - lastTime;
        lastTime = now;

        game.update(deltaTime);
        game.render();

        requestAnimationFrame(gameLoop);
    }

    // Start the game loop
    gameLoop();

    console.log('Game initialized successfully!');
});
