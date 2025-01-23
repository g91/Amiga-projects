import socket
import threading
from typing import Dict

class ChatServer:
    def __init__(self, host: str = '0.0.0.0', port: int = 5000):
        self.host = host
        self.port = port
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.clients: Dict[socket.socket, str] = {}
        self.commands = {
            'help': self.help_command,
            'echo': self.echo_command,
            'users': self.users_command,
            'nick': self.nick_command,
            'quit': self.quit_command
        }

    def start(self):
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        print(f"Server started on {self.host}:{self.port}")

        while True:
            client_socket, address = self.server_socket.accept()
            print(f"New connection from {address}")
            self.clients[client_socket] = f"User_{len(self.clients)}"
            
            client_thread = threading.Thread(
                target=self.handle_client,
                args=(client_socket,)
            )
            client_thread.start()

    def broadcast(self, message: str, sender_socket=None):
        """Send message to all clients except sender"""
        print(f"[DEBUG] Broadcasting: {message}")
        for client in self.clients:
            if client != sender_socket:
                try:
                    self.send_message(client, message)
                except:
                    self.remove_client(client)

    def send_message(self, client: socket.socket, message: str):
        """Send formatted message to client"""
        try:
            # Protocol format: MSG:message text\n
            formatted = f"MSG:{message}\n"
            print(f"[DEBUG] Sending to {self.clients[client]}: {formatted.strip()}")
            client.send(formatted.encode())
        except Exception as e:
            print(f"[DEBUG] Error sending to {self.clients[client]}: {str(e)}")
            self.remove_client(client)

    def remove_client(self, client: socket.socket):
        """Remove disconnected client"""
        if client in self.clients:
            username = self.clients[client]
            print(f"[DEBUG] Removing client {username}")
            del self.clients[client]
            client.close()
            self.broadcast(f"{username} has left the chat")

    def handle_client(self, client_socket: socket.socket):
        """Handle individual client connection"""
        welcome = f"Welcome {self.clients[client_socket]}! Type /help for commands."
        print(f"[DEBUG] Sending welcome: {welcome}")
        self.send_message(client_socket, welcome)
        self.broadcast(f"{self.clients[client_socket]} has joined the chat", client_socket)

        while True:
            try:
                message = client_socket.recv(1024).decode().strip()
                print(f"[DEBUG] Received from {self.clients[client_socket]}: {message}")
                
                if not message:
                    break

                if message.startswith('/'):
                    cmd = message[1:]
                    print(f"[DEBUG] Processing command: {cmd}")
                    self.handle_command(client_socket, cmd)
                else:
                    broadcast_msg = f"{self.clients[client_socket]}: {message}"
                    print(f"[DEBUG] Broadcasting message: {broadcast_msg}")
                    self.broadcast(broadcast_msg, client_socket)
            except:
                break

        self.remove_client(client_socket)

    def handle_command(self, client: socket.socket, command_str: str):
        """Process commands starting with /"""
        parts = command_str.split(' ', 1)
        command = parts[0].lower()
        args = parts[1] if len(parts) > 1 else ''

        if command in self.commands:
            self.commands[command](client, args)
        else:
            self.send_message(client, "Unknown command. Type /help for available commands.")

    def help_command(self, client: socket.socket, args: str):
        """Show available commands"""
        help_text = """Available commands:
/help - Show this help message
/echo <message> - Echo a message back to you
/users - List connected users
/nick <name> - Change your nickname
/quit - Disconnect from server"""
        self.send_message(client, help_text)

    def echo_command(self, client: socket.socket, args: str):
        """Echo message back to sender"""
        if args:
            self.send_message(client, f"Echo: {args}")
        else:
            self.send_message(client, "Usage: /echo <message>")

    def users_command(self, client: socket.socket, args: str):
        """List connected users"""
        users = ', '.join(self.clients.values())
        self.send_message(client, f"Connected users: {users}")

    def nick_command(self, client: socket.socket, args: str):
        """Change user nickname"""
        if args:
            old_name = self.clients[client]
            self.clients[client] = args
            self.broadcast(f"{old_name} is now known as {args}")
        else:
            self.send_message(client, "Usage: /nick <new_nickname>")

    def quit_command(self, client: socket.socket, args: str):
        """Disconnect client"""
        self.send_message(client, "Goodbye!")
        self.remove_client(client)

if __name__ == '__main__':
    server = ChatServer()
    try:
        server.start()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        server.server_socket.close()
