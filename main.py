#!/usr/bin/env python3
"""
Enhanced RFID Socket Server
Writes to Block 8 (IDs) and Block 9 (WAREHOUSE_IN)
Includes retry mechanism for failed writes
"""

import socket
import threading
import time
import json
from datetime import datetime

# Configuration
SERVER_HOST = "0.0.0.0"
SERVER_PORT = 1234
MAX_WRITE_RETRIES = 3
RETRY_DELAY = 0.5  # seconds

class EnhancedRFIDServer:
    def __init__(self):
        self.clients = {}
        self.running = False
        self.next_id = 1
        self.used_ids = set()
        self.load_id_state()
        
    def load_id_state(self):
        """Load the current ID state from file"""
        try:
            with open("id_state.json", "r") as f:
                data = json.load(f)
                self.next_id = data.get("next_id", 1)
                self.used_ids = set(data.get("used_ids", []))
                print(f"Loaded ID state: next_id={self.next_id}, used_ids count={len(self.used_ids)}")
        except FileNotFoundError:
            print("No previous ID state found, starting fresh")
        except Exception as e:
            print(f"Error loading ID state: {e}")
    
    def save_id_state(self):
        """Save the current ID state to file"""
        try:
            data = {
                "next_id": self.next_id,
                "used_ids": list(self.used_ids)
            }
            with open("id_state.json", "w") as f:
                json.dump(data, f)
        except Exception as e:
            print(f"Error saving ID state: {e}")
    
    def get_next_available_id(self):
        """Get the next available ID"""
        while self.next_id in self.used_ids:
            self.next_id += 1
        
        current_id = self.next_id
        self.used_ids.add(current_id)
        self.next_id += 1
        self.save_id_state()
        return str(current_id).zfill(8)  # Pad to 8 digits
        
    def start_server(self):
        try:
            server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_socket.bind((SERVER_HOST, SERVER_PORT))
            server_socket.listen(5)
            
            self.running = True
            print(f"Enhanced RFID Server started on {SERVER_HOST}:{SERVER_PORT}")
            
            while self.running:
                try:
                    client_socket, client_address = server_socket.accept()
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client_socket, client_address),
                        daemon=True
                    )
                    client_thread.start()
                except Exception as e:
                    print(f"Accept error: {e}")
                    
        except Exception as e:
            print(f"Server error: {e}")
    
    def handle_client(self, client_socket, client_address):
        client_ip = client_address[0]
        print(f"Client connected: {client_ip}")
        
        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                    
                message = data.decode('utf-8').strip()
                if not message:
                    continue
                    
                print(f"Received from {client_ip}: {message}")
                
                response = self.process_message(message, client_ip, client_socket)
                if response:
                    client_socket.send(f"{response}\n".encode('utf-8'))
                    print(f"Sent to {client_ip}: {response}")
                    
        except Exception as e:
            print(f"Client {client_ip} error: {e}")
        finally:
            client_socket.close()
            print(f"Client {client_ip} disconnected")
    
    def send_write_command(self, client_socket, block, data, max_retries=MAX_WRITE_RETRIES):
        """Send write command with retry mechanism"""
        for attempt in range(max_retries):
            try:
                write_cmd = f"WRITE_BLOCK|BLOCK:{block}|DATA:{data}\n"
                client_socket.send(write_cmd.encode('utf-8'))
                print(f"Write attempt {attempt + 1}/{max_retries} - Block {block}: {data}")
                
                # Wait for response
                client_socket.settimeout(5.0)  # 5 second timeout
                response = client_socket.recv(1024).decode('utf-8').strip()
                client_socket.settimeout(None)  # Reset timeout
                
                if response == "WRITE_SUCCESS":
                    print(f"Successfully wrote to Block {block}")
                    return True
                elif response == "WRITE_FAILED":
                    print(f"Write failed for Block {block}, attempt {attempt + 1}")
                    if attempt < max_retries - 1:
                        time.sleep(RETRY_DELAY)
                        continue
                else:
                    print(f"Unexpected response: {response}")
                    
            except socket.timeout:
                print(f"Timeout waiting for write response, attempt {attempt + 1}")
            except Exception as e:
                print(f"Write error on attempt {attempt + 1}: {e}")
                
            if attempt < max_retries - 1:
                time.sleep(RETRY_DELAY)
        
        print(f"Failed to write to Block {block} after {max_retries} attempts")
        return False
    
    def read_block(self, client_socket, block):
        """Read data from a specific block"""
        try:
            read_cmd = f"READ_BLOCK|BLOCK:{block}\n"
            client_socket.send(read_cmd.encode('utf-8'))
            
            client_socket.settimeout(5.0)
            response = client_socket.recv(1024).decode('utf-8').strip()
            client_socket.settimeout(None)
            
            if response.startswith("READ_SUCCESS|DATA:"):
                data = response.split("READ_SUCCESS|DATA:")[1]
                return data.strip()
            else:
                print(f"Failed to read Block {block}: {response}")
                return None
                
        except Exception as e:
            print(f"Read error for Block {block}: {e}")
            return None
    
    def process_message(self, message, client_ip, client_socket):
        try:
            parts = message.split('|')
            command = parts[0]
            
            if command == "READER_WRITER_READY":
                return "ACK_READER_WRITER_READY"
            
            elif command == "RFID_DETECTED":
                # Expected format: RFID_DETECTED|UID:xxxxx
                card_uid = ""
                
                for part in parts[1:]:
                    if part.startswith("UID:"):
                        card_uid = part[4:]
                        break
                
                if not card_uid:
                    return "ERROR_NO_UID"
                
                print(f"Processing RFID card: {card_uid}")
                
                # Read current Block 8 data to check if ID already exists
                current_block8_data = self.read_block(client_socket, 8)
                
                # Determine what to write to Block 8
                if current_block8_data and current_block8_data.strip() and current_block8_data != "EMPTY":
                    # Block 8 already has data, don't overwrite
                    block8_data = current_block8_data
                    print(f"Block 8 already contains ID: {block8_data}")
                    write_block8 = False
                else:
                    # Block 8 is empty, assign new ID
                    block8_data = self.get_next_available_id()
                    print(f"Assigning new ID to Block 8: {block8_data}")
                    write_block8 = True
                
                # Always write WAREHOUSE_IN to Block 9
                block9_data = "WAREHOUSE_IN"
                
                # Perform writes
                write_success = True
                
                if write_block8:
                    if not self.send_write_command(client_socket, 8, block8_data):
                        write_success = False
                
                if not self.send_write_command(client_socket, 9, block9_data):
                    write_success = False
                
                # Log the operation
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                status = "SUCCESS" if write_success else "PARTIAL_FAILURE"
                log_entry = (f"[{timestamp}] RFID Operation - "
                           f"UID: {card_uid}, "
                           f"Block8: {block8_data} {'(written)' if write_block8 else '(existing)'}, "
                           f"Block9: {block9_data} (written), "
                           f"Status: {status}")
                
                print(log_entry)
                
                # Write to log file
                try:
                    with open("rfid_operations.log", "a") as f:
                        f.write(log_entry + "\n")
                except Exception as e:
                    print(f"File write error: {e}")
                
                if write_success:
                    return "ACK_WRITE_SUCCESS"
                else:
                    return "ACK_WRITE_PARTIAL"
            
            elif command == "RFID_LOG":
                # Legacy support for old log format
                card_uid = ""
                block8_data = ""
                block9_data = ""
                sequence = ""
                
                for part in parts[1:]:
                    if part.startswith("UID:"):
                        card_uid = part[4:]
                    elif part.startswith("BLOCK8:"):
                        block8_data = part[7:]
                    elif part.startswith("BLOCK9:"):
                        block9_data = part[7:]
                    elif part.startswith("SEQ:"):
                        sequence = part[4:]
                
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                log_entry = (f"[{timestamp}] RFID Log - "
                           f"UID: {card_uid}, "
                           f"Block8: {block8_data}, "
                           f"Block9: {block9_data}, "
                           f"Sequence: {sequence}")
                
                print(log_entry)
                
                try:
                    with open("rfid_operations.log", "a") as f:
                        f.write(log_entry + "\n")
                except Exception as e:
                    print(f"File write error: {e}")
                
                return "ACK_LOGGED"
            
            elif command == "HEARTBEAT":
                return "HEARTBEAT_ACK"
            
            elif command in ["WRITE_SUCCESS", "WRITE_FAILED", "READ_SUCCESS"]:
                # These are responses to our commands, don't send a response back
                return None
            
            else:
                return "ERROR_UNKNOWN_COMMAND"
                
        except Exception as e:
            print(f"Message processing error: {e}")
            return "ERROR_PROCESSING"

def main():
    print("=== Enhanced RFID Server with Block Writing ===")
    print("Features:")
    print("- Automatic ID assignment to Block 8")
    print("- WAREHOUSE_IN writing to Block 9")
    print("- Write retry mechanism")
    print("- ID state persistence")
    print()
    
    server = EnhancedRFIDServer()
    
    try:
        server.start_server()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        server.running = False

if __name__ == "__main__":
    main()