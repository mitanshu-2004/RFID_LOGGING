#!/usr/bin/env python3
"""
RFID Operations GUI Monitor
Real-time monitoring of RFID operations with both IN and OUT devices
"""

import tkinter as tk
from tkinter import ttk, messagebox
import threading
import socket
import json
import time
from datetime import datetime
import queue
import os
from collections import defaultdict
import csv

class RFIDMonitorGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("RFID Warehouse Operations Monitor")
        self.root.geometry("1200x800")
        self.root.configure(bg='#2b2b2b')
        
        # Server configuration
        self.server_host = "0.0.0.0"
        self.server_port = 1234
        self.server_socket = None
        self.running = False
        
        # Data tracking
        self.clients = {}
        self.operations_queue = queue.Queue()
        self.next_id = 1
        self.used_ids = set()
        self.device_stats = defaultdict(lambda: {'in_count': 0, 'out_count': 0, 'total': 0})

        # CSV file for logging
        self.csv_filename = "rfid_operations_log.csv"
        self.init_csv_file()
        
        # Load saved state
        self.load_state()
        
        # Create GUI
        self.setup_gui()
        
        # Start server thread
        self.start_server_thread()
        
        # Start GUI update thread
        self.start_gui_update_thread()
        
        # Bind close event
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    def init_csv_file(self):
        """Create CSV file with header if it doesn't exist"""
        if not os.path.exists(self.csv_filename):
            with open(self.csv_filename, mode='w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(["Timestamp", "Device", "UID", "Operation", "Block8", "Block9", "Status"])

    def setup_gui(self):
        # Create main style
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('Title.TLabel', font=('Arial', 16, 'bold'), background='#2b2b2b', foreground='white')
        style.configure('Header.TLabel', font=('Arial', 12, 'bold'), background='#2b2b2b', foreground='white')
        style.configure('Status.TLabel', font=('Arial', 10), background='#2b2b2b')
        
        # Title
        title_frame = tk.Frame(self.root, bg='#2b2b2b', height=60)
        title_frame.pack(fill='x', padx=10, pady=5)
        title_frame.pack_propagate(False)
        
        title_label = ttk.Label(title_frame, text="🏭 RFID Warehouse Operations Monitor", style='Title.TLabel')
        title_label.pack(pady=15)
        
        # Main container
        main_container = tk.Frame(self.root, bg='#2b2b2b')
        main_container.pack(fill='both', expand=True, padx=10, pady=5)
        
        # Top row - Status and Stats
        top_frame = tk.Frame(main_container, bg='#2b2b2b')
        top_frame.pack(fill='x', pady=(0, 10))
        
        # Server status frame
        status_frame = tk.LabelFrame(top_frame, text="Server Status", bg='#3b3b3b', fg='white', font=('Arial', 10, 'bold'))
        status_frame.pack(side='left', fill='both', expand=True, padx=(0, 5))
        
        self.server_status_label = ttk.Label(status_frame, text="🔴 Stopped", style='Status.TLabel')
        self.server_status_label.pack(pady=5)
        
        self.client_count_label = ttk.Label(status_frame, text="Connected Devices: 0", style='Status.TLabel')
        self.client_count_label.pack(pady=2)
        
        self.port_label = ttk.Label(status_frame, text=f"Port: {self.server_port}", style='Status.TLabel')
        self.port_label.pack(pady=2)
        
        # Statistics frame
        stats_frame = tk.LabelFrame(top_frame, text="Operations Statistics", bg='#3b3b3b', fg='white', font=('Arial', 10, 'bold'))
        stats_frame.pack(side='right', fill='both', expand=True, padx=(5, 0))
        
        self.total_operations_label = ttk.Label(stats_frame, text="Total Operations: 0", style='Status.TLabel')
        self.total_operations_label.pack(pady=2)
        
        self.in_operations_label = ttk.Label(stats_frame, text="IN Operations: 0", style='Status.TLabel')
        self.in_operations_label.pack(pady=2)
        
        self.out_operations_label = ttk.Label(stats_frame, text="OUT Operations: 0", style='Status.TLabel')
        self.out_operations_label.pack(pady=2)
        
        self.next_id_label = ttk.Label(stats_frame, text=f"Next ID: {self.next_id}", style='Status.TLabel')
        self.next_id_label.pack(pady=2)
        
        # Middle row - Connected Devices
        devices_frame = tk.LabelFrame(main_container, text="Connected Devices", bg='#3b3b3b', fg='white', font=('Arial', 12, 'bold'))
        devices_frame.pack(fill='x', pady=(0, 10))
        
        devices_columns = ('IP', 'Type', 'Status', 'Last Activity', 'IN Count', 'OUT Count', 'Total')
        self.devices_tree = ttk.Treeview(devices_frame, columns=devices_columns, show='headings', height=4)
        
        for col in devices_columns:
            self.devices_tree.heading(col, text=col)
            self.devices_tree.column(col, width=120, anchor='center')
        
        devices_scrollbar = ttk.Scrollbar(devices_frame, orient='vertical', command=self.devices_tree.yview)
        self.devices_tree.configure(yscrollcommand=devices_scrollbar.set)
        
        self.devices_tree.pack(side='left', fill='both', expand=True, padx=5, pady=5)
        devices_scrollbar.pack(side='right', fill='y', pady=5)
        
        # Bottom row - Real-time Operations Log
        log_frame = tk.LabelFrame(main_container, text="Latest Operation", bg='#3b3b3b', fg='white', font=('Arial', 12, 'bold'))
        log_frame.pack(fill='both', expand=True)
        
        log_columns = ('Time', 'Device', 'UID', 'Operation', 'Block 8', 'Block 9', 'Status')
        self.log_tree = ttk.Treeview(log_frame, columns=log_columns, show='headings')
        
        column_widths = {'Time': 150, 'Device': 120, 'UID': 120, 'Operation': 100, 
                        'Block 8': 120, 'Block 9': 120, 'Status': 100}
        
        for col in log_columns:
            self.log_tree.heading(col, text=col)
            self.log_tree.column(col, width=column_widths.get(col, 100), anchor='center')
        
        log_scrollbar_y = ttk.Scrollbar(log_frame, orient='vertical', command=self.log_tree.yview)
        log_scrollbar_x = ttk.Scrollbar(log_frame, orient='horizontal', command=self.log_tree.xview)
        self.log_tree.configure(yscrollcommand=log_scrollbar_y.set, xscrollcommand=log_scrollbar_x.set)
        
        self.log_tree.pack(side='left', fill='both', expand=True, padx=5, pady=5)
        log_scrollbar_y.pack(side='right', fill='y', pady=5)
        log_scrollbar_x.pack(side='bottom', fill='x', padx=5)
        
        # Control buttons
        button_frame = tk.Frame(main_container, bg='#2b2b2b')
        button_frame.pack(fill='x', pady=5)
        
        self.restart_server_btn = tk.Button(button_frame, text="Restart Server", command=self.restart_server,
                                           bg='#45b7d1', fg='white', font=('Arial', 10, 'bold'))
        self.restart_server_btn.pack(side='right', padx=5)

    def load_state(self):
        """Load saved state from file"""
        try:
            if os.path.exists("gui_state.json"):
                with open("gui_state.json", "r") as f:
                    data = json.load(f)
                    self.next_id = data.get("next_id", 1)
                    self.used_ids = set(data.get("used_ids", []))
                    self.device_stats = defaultdict(lambda: {'in_count': 0, 'out_count': 0, 'total': 0})
                    for ip, stats in data.get("device_stats", {}).items():
                        self.device_stats[ip] = stats
        except Exception as e:
            print(f"Error loading state: {e}")

    def save_state(self):
        """Save current state to file"""
        try:
            data = {
                "next_id": self.next_id,
                "used_ids": list(self.used_ids),
                "device_stats": dict(self.device_stats),
                "timestamp": datetime.now().isoformat()
            }
            with open("gui_state.json", "w") as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"Error saving state: {e}")

    def get_next_available_id(self):
        while self.next_id in self.used_ids:
            self.next_id += 1
        current_id = self.next_id
        self.used_ids.add(current_id)
        self.next_id += 1
        self.save_state()
        return str(current_id).zfill(8)

    def start_server_thread(self):
        threading.Thread(target=self.run_server, daemon=True).start()

    def start_gui_update_thread(self):
        threading.Thread(target=self.update_gui_loop, daemon=True).start()

    def run_server(self):
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.server_host, self.server_port))
            self.server_socket.listen(5)
            self.running = True
            self.operations_queue.put(('server_status', f"🟢 Running on {self.server_host}:{self.server_port}"))
            while self.running:
                try:
                    client_socket, client_address = self.server_socket.accept()
                    threading.Thread(target=self.handle_client, args=(client_socket, client_address), daemon=True).start()
                except Exception as e:
                    if self.running:
                        print(f"Accept error: {e}")
        except Exception as e:
            self.operations_queue.put(('server_status', f"🔴 Error: {e}"))

    def handle_client(self, client_socket, client_address):
        client_ip = client_address[0]
        self.clients[client_ip] = {
            'socket': client_socket,
            'connected_time': datetime.now(),
            'last_activity': datetime.now(),
            'type': 'Unknown',
            'status': 'Connected'
        }
        self.operations_queue.put(('client_connected', client_ip))
        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                message = data.decode('utf-8').strip()
                if not message:
                    continue
                self.clients[client_ip]['last_activity'] = datetime.now()
                response = self.process_message(message, client_ip, client_socket)
                if response:
                    client_socket.send(f"{response}\n".encode('utf-8'))
        except Exception as e:
            print(f"Client {client_ip} error: {e}")
        finally:
            client_socket.close()
            if client_ip in self.clients:
                del self.clients[client_ip]
            self.operations_queue.put(('client_disconnected', client_ip))

    def process_message(self, message, client_ip, client_socket):
        try:
            parts = message.split('|')
            command = parts[0]
            if command == "READER_WRITER_READY":
                self.clients[client_ip]['type'] = 'RFID Reader'
                self.clients[client_ip]['status'] = 'Ready'
                return "ACK_READER_WRITER_READY"
            elif command == "RFID_LOG":
                card_uid, action, block8_data, block9_data = "", "", "", ""
                for part in parts[1:]:
                    if part.startswith("UID:"):
                        card_uid = part[4:]
                    elif part.startswith("ACTION:"):
                        action = part[7:]
                    elif part.startswith("BLOCK8:"):
                        block8_data = part[7:]
                    elif part.startswith("BLOCK9:"):
                        block9_data = part[7:]
                if "WAREHOUSE_IN" in block9_data:
                    operation_type = "IN"
                    self.device_stats[client_ip]['in_count'] += 1
                elif "WAREHOUSE_OUT" in block9_data:
                    operation_type = "OUT"
                    self.device_stats[client_ip]['out_count'] += 1
                else:
                    operation_type = action if action else "UNKNOWN"
                self.device_stats[client_ip]['total'] += 1
                self.clients[client_ip]['type'] = f'RFID {operation_type} Reader'
                operation_data = {
                    'timestamp': datetime.now().strftime("%H:%M:%S"),
                    'device': client_ip,
                    'uid': card_uid,
                    'operation': operation_type,
                    'block8': block8_data,
                    'block9': block9_data,
                    'status': 'Success'
                }
                self.operations_queue.put(('rfid_operation', operation_data))
                return "ACK_LOGGED"
            elif command == "HEARTBEAT":
                return "HEARTBEAT_ACK"
            else:
                return "ERROR_UNKNOWN_COMMAND"
        except Exception as e:
            print(f"Message processing error: {e}")
            return "ERROR_PROCESSING"

    def update_gui_loop(self):
        while True:
            try:
                while not self.operations_queue.empty():
                    operation_type, data = self.operations_queue.get_nowait()
                    if operation_type == 'server_status':
                        self.root.after(0, self.update_server_status, data)
                    elif operation_type in ('client_connected', 'client_disconnected'):
                        self.root.after(0, self.update_devices_display)
                    elif operation_type == 'rfid_operation':
                        self.root.after(0, self.add_operation_to_log, data)
                self.root.after(0, self.update_statistics)
                self.root.after(0, self.update_devices_display)
                time.sleep(1)
            except Exception as e:
                print(f"GUI update error: {e}")
                time.sleep(1)

    def update_server_status(self, status):
        self.server_status_label.config(text=status)

    def update_devices_display(self):
        for item in self.devices_tree.get_children():
            self.devices_tree.delete(item)
        for ip, info in self.clients.items():
            stats = self.device_stats[ip]
            last_activity = info['last_activity'].strftime("%H:%M:%S")
            self.devices_tree.insert('', 'end', values=(
                ip, info['type'], info['status'], last_activity,
                stats['in_count'], stats['out_count'], stats['total']
            ))
        self.client_count_label.config(text=f"Connected Devices: {len(self.clients)}")

    def update_statistics(self):
        total_operations = sum(stats['total'] for stats in self.device_stats.values())
        total_in = sum(stats['in_count'] for stats in self.device_stats.values())
        total_out = sum(stats['out_count'] for stats in self.device_stats.values())
        self.total_operations_label.config(text=f"Total Operations: {total_operations}")
        self.in_operations_label.config(text=f"IN Operations: {total_in}")
        self.out_operations_label.config(text=f"OUT Operations: {total_out}")
        self.next_id_label.config(text=f"Next ID: {self.next_id}")

    def add_operation_to_log(self, operation_data):
        # Append to CSV
        try:
            with open(self.csv_filename, mode='a', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([
                    operation_data['timestamp'],
                    operation_data['device'],
                    operation_data['uid'],
                    operation_data['operation'],
                    operation_data['block8'],
                    operation_data['block9'],
                    operation_data['status']
                ])
        except Exception as e:
            print(f"CSV logging error: {e}")

        # Show only the latest operation
        for item in self.log_tree.get_children():
            self.log_tree.delete(item)
        self.log_tree.insert('', 0, values=(
            operation_data['timestamp'],
            operation_data['device'],
            operation_data['uid'],
            operation_data['operation'],
            operation_data['block8'],
            operation_data['block9'],
            operation_data['status']
        ))

    def restart_server(self):
        if messagebox.askyesno("Confirm", "Restart the RFID server?"):
            self.running = False
            if self.server_socket:
                self.server_socket.close()
            self.clients.clear()
            self.root.after(2000, self.restart_server_delayed)

    def restart_server_delayed(self):
        self.start_server_thread()

    def on_closing(self):
        self.save_state()
        self.running = False
        if self.server_socket:
            self.server_socket.close()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = RFIDMonitorGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()
